"""Attack: the dogfight a craft flies for the Attack order.

Purpose:
    A craft with the Attack order picks the nearest craft its order names,
    turns toward it, closes, and fires its cannons while the target sits
    inside its aim cone and firing range. Inside the target's clearance
    (``tactics.clearance``, which grows with the target's size), with the
    target ahead, or anywhere when the target is a freighter, starship or
    platform, it breaks off a quarter turn to the side away from the
    target, or, when another bulky craft lies in that heading's way
    (``avoidance.in_the_way``), away from the two of them alike; it holds
    that heading until it is outside the clearance, and comes round again; with
    an enemy on its tail that targets it, it weaves (``weave``) until the
    tail is clear or the weave's swerves are flown, steering clear of every
    bulky craft its own target among them, then fights again. Skill
    sets the aim cone, the firing range, the burst and how soon it looks
    again for a nearer target. Lined up or not, it fires a guided warhead
    at its target when ``warheads.choose`` lets it: its lock declared, a
    warhead that suits the target, inside 1997's limits.

    With no target its order names in the battle, a craft an enemy is after
    (``pursuer``) fights that enemy while it may fight back (``fights_back``:
    it has cannons, it can move, and its hull is at FIGHT_BACK_HULL or more),
    and otherwise weaves when the enemy sits on its tail (rulings 34 and 43).
    ``fight`` is the dogfight itself, at the targets a behavior gives it:
    ``home`` flies it for a craft going home that turns on its attacker, and
    for a craft with no way out (rulings 31 and 33).

    A player's commands come first, as the 1997 target orders took them:
    the craft an attack or cover order named is the target while the craft
    may fight it, and the one an ignore order named is never picked
    (``tactics.commanded``). An evade command drops the target and starts
    a weave that flies all its swerves, chased or not; the ``evaded`` action
    tells the engine it was taken up.

    The rules' shape follows Starshatter Open's fighter pilot (target choice,
    fire control, breaking off), written new in this program's integer
    units; no Starshatter code is copied. The firing numbers are the game's
    own: the 1997 pilot's ranges, cone and bursts.

Phases, the notebook's phase byte:

    SEEK    no target: hold fire, keep course, look again each think; an
            enemy on the tail starts a weave
    ATTACK  steer at the target, fire when lined up; each entry to ATTACK,
            and each new target, starts an attack run
    BREAK   inside the target's clearance, ahead, or anywhere for a bulky
            target: hold a heading a quarter turn away from the target's
            side, or away from it and the bulky craft in that way, until
            outside the clearance, for BREAK_LIMIT_TICKS at most
    EVADE   an enemy on the tail: weave

The scratch bytes, little-endian: u16 target index, u16 its signature,
u16 yaw and u16 pitch a break holds, u32 the tick to look again for a
nearer target, then the weave's state (``weave.STATE_BYTES``), then u8 1
while the weave flies an evade command, else 0; then u8 the warheads fired
in this attack run, u8 1 while a shot at a starfighter or transport waits
for the lock to lapse, else 0, and u8 the last reason the craft held its
warheads (``warheads.Held``); the rest zero. In SEEK only the weave's bytes
are kept, so a weave's rest outlasts the search.

Invariants:
    - The decision follows from the view, the question and the notebook;
      random choices come from the craft's dice in its notebook.
    - It names only what the question showed: its target is a contact, and
      it fires only the cannon groups the view counts.
    - It targets only craft the engine marks hostile (``tactics.can_fight``).
    - Only a small craft is ever after a craft: a freighter, a starship or a
      platform shooting at it is no pursuer (ruling 42).
    - A break turns away from the side its target is on, so the turn never
      swings the nose across the target; the dice pick the side only when
      the target is dead ahead. A break another bulky craft's clearance
      blocks heads away from both instead, unless the craft lies nearly in
      line between them (``escape``) or the turn there swings the nose
      across either; then it keeps the quarter turn, and avoidance turns it
      round the other.
    - Unless it weaves, a craft inside a bulky target's clearance breaks
      off from it rather than steer at it; a weave steers clear of it, and
      avoidance turns a break clear of it as of any bulky craft.
    - A fighting craft thinks every FIGHT_INTERVAL ticks, whatever its skill.
"""

from __future__ import annotations

import dataclasses
import logging
import struct
import typing as t

from .. import dice
from .. import geometry
from .. import notebook
from .. import tactics
from .. import warheads
from .. import weave
from ..world import Actions
from ..world import CANNON_GROUPS
from ..world import CannonGroup
from ..world import Commands
from ..world import Contact
from ..world import CraftView
from ..world import HOLD_FIRE
from ..world import LinkMode
from ..world import Lock
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from ..world import Warhead
from . import avoidance
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

BREAK_CONE = 0x2000
# A break ends at this many ticks even inside the clearance, so a target
# that keeps up cannot hold a craft flying straight with its cannons cold: 4
# real seconds, the slowest quarter turn measured in test flights (420 ticks,
# craft type 4) and 580 ticks more, about 9,200 units at that type's best
# pace, more than a freighter's whole clearance (7,816).
BREAK_LIMIT_TICKS = 1000
# Within this many yaw units of the nose a target is dead ahead: the
# arctangent's own error (``geometry``), too small to name a side.
DEAD_AHEAD = 4
# The length escape scales each direction to before it sums them.
UNIT = 0x10000
# An enemy within this range, more than this angle off the nose and
# targeting the craft, is on its tail.
TAIL_RANGE = 0x8000
TAIL_ANGLE = 0x6000
# A craft an enemy is after fights back while its hull, the hull strength less
# the hull damage, is at least this share of the strength (ruling 34: three
# quarters); shields never count.
FIGHT_BACK_HULL = (3, 4)

_SCRATCH = struct.Struct("<HHHHI")
_ORDERED = struct.Struct("<B")
_WARHEADS = struct.Struct("<BBB")
_U32_MAX = 0xFFFFFFFF


@dataclasses.dataclass(frozen=True)
class Memory:
    """The scratch bytes' fields."""

    target: int = NO_OBJECT
    signature: int = 0
    yaw: int = 0
    pitch: int = geometry.LEVEL
    look_again: int = 0
    weave: weave.Weave = weave.Weave()
    ordered: int = 0
    shots: int = 0
    fresh: int = 0
    held: int = warheads.Held.NONE

    @classmethod
    def read(cls, scratch: bytes) -> Memory:
        """Return the memory scratch bytes hold."""
        ordered_at = _SCRATCH.size + weave.STATE_BYTES
        return cls(
            *_SCRATCH.unpack_from(scratch),
            weave.Weave.read(scratch[_SCRATCH.size :]),
            *_ORDERED.unpack_from(scratch, ordered_at),
            *_WARHEADS.unpack_from(scratch, ordered_at + _ORDERED.size),
        )

    def write(self) -> bytes:
        """Return the memory as scratch bytes."""
        packed = _SCRATCH.pack(
            self.target, self.signature, self.yaw, self.pitch, self.look_again
        )
        packed += self.weave.write() + _ORDERED.pack(self.ordered)
        packed += _WARHEADS.pack(self.shots, self.fresh, self.held)
        return packed + bytes(notebook.SCRATCH_BYTES - len(packed))


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move in its dogfight: at the targets its order
    names, or, with none in the battle, at the enemy after it while it may
    fight back; a player's commands first."""
    if view.commands & Commands.EVADE:
        return _evade_ordered(view, question)
    targets = tactics.commanded(view, question, tactics.order_targets(view, question))
    if not targets:
        chaser = pursuer(view, question)
        if chaser is not None and fights_back(view, question, chaser):
            targets = [chaser]
    return fight(view, question, targets)


def fight(view: CraftView, question: Question, targets: list[Contact]) -> Decision:
    """Return the craft's next move in a dogfight at the nearest of targets,
    each a contact it may fight."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    memory = Memory.read(book.scratch)
    if book.phase == SEEK:
        memory = Memory(weave=memory.weave)
    if memory.fresh and view.lock_state != Lock.DECLARED:
        memory = dataclasses.replace(memory, fresh=0)
    tier = tactics.skill_tier(view.skill)
    interval = None if view.think_interval == FIGHT_INTERVAL else FIGHT_INTERVAL

    if book.phase == BREAK and break_holds(view, question, book, memory):
        actions = Actions(
            steer=Steer(memory.yaw, memory.pitch, view.roll),
            throttle=FULL_THROTTLE,
            think_interval=interval,
            lasers=(HOLD_FIRE, HOLD_FIRE),
        )
        return Decision(book.encode(), actions)
    if book.phase == EVADE and memory.weave.flying:
        chased = tail_threat(view, question) is not None or _ordered_on(memory, tick)
        flown, move, state = weave.fly(view, memory.weave, chased, book.dice, tick)
        memory = dataclasses.replace(
            memory, weave=flown, ordered=memory.ordered if flown.flying else 0
        )
        book = dataclasses.replace(book, dice=state, scratch=memory.write())
        if move is not None:
            move = _clear_of_all(view, question, move)
            return Decision(book.encode(), _weave_actions(move, interval))

    target, memory = _choose_target(view, targets, memory, tier, tick)
    if target is None:
        return _seek(view, question, book, memory, interval)

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

    keep = tactics.clearance(target)
    if reach < keep and (off_nose < BREAK_CONE or tactics.is_bulky(target)):
        held_yaw, state = break_heading(view.yaw, yaw, book.dice)
        held_pitch, blocked = geometry.LEVEL, NO_OBJECT
        obstacle = avoidance.in_the_way(
            view, question, held_yaw, held_pitch, target.object_index
        )
        away = None if obstacle is None else escape(view, target, obstacle.contact)
        crosses = away is not None and any(
            geometry.sweeps(view.yaw, away[0], toward) for toward in (yaw, obstacle.yaw)
        )
        if away is not None and not crosses:
            (held_yaw, held_pitch), blocked = away, obstacle.contact.object_index
        logger.debug(
            "attack.break object=%d target=%d distance=%d clearance=%d yaw=%d "
            "pitch=%d blocked=%d",
            view.object_index,
            target.object_index,
            reach,
            keep,
            held_yaw,
            held_pitch,
            blocked,
        )
        memory = dataclasses.replace(memory, yaw=held_yaw, pitch=held_pitch)
        return _hold(view, book, state, BREAK, tick, memory, target, interval)

    threat = tail_threat(view, question)
    if threat is not None and not lined_up and weave.may_start(memory.weave, tick):
        started, move, state = weave.start(
            view, threat.object_index, memory.weave, book.dice, tick
        )
        memory = dataclasses.replace(memory, weave=started)
        book = dataclasses.replace(
            book.in_phase(EVADE, tick), dice=state, scratch=memory.write()
        )
        move = _clear_of_all(view, question, move)
        actions = dataclasses.replace(
            _weave_actions(move, interval), target=target.object_index
        )
        return Decision(book.encode(), actions)

    if book.phase != ATTACK:
        book = book.in_phase(ATTACK, tick)
        memory = dataclasses.replace(memory, shots=0)
    shot, memory = _warhead(view, question, target, memory)
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
        warhead=shot,
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _warhead(
    view: CraftView, question: Question, target: Contact, memory: Memory
) -> tuple[Warhead | None, Memory]:
    """Return the guided warhead the craft fires at its target, None when it
    holds, and the memory counting the shot; the reason it holds is logged
    when it changes."""
    if view.warhead_launcher_count == 0:
        return None, memory
    choice = warheads.choose(view, question, target, memory.shots, bool(memory.fresh))
    if choice.warhead is None:
        if choice.held != memory.held:
            logger.debug(
                'attack.warhead_held object=%d target=%d reason="%s" lock=%d '
                "lock_state=%d coming=%d coming_damage=%d shots=%d",
                view.object_index,
                target.object_index,
                choice.held.name.lower(),
                view.lock_ticks,
                view.lock_state,
                target.warheads_coming,
                target.warheads_coming_damage,
                memory.shots,
            )
        return None, dataclasses.replace(memory, held=choice.held)
    launcher = choice.warhead.launcher
    shots = memory.shots + 1
    logger.info(
        "attack.warhead object=%d target=%d launcher=%d type=%d lock=%d coming=%d "
        "shots=%d",
        view.object_index,
        target.object_index,
        launcher,
        view.warhead_type[launcher],
        view.lock_ticks,
        target.warheads_coming,
        shots,
    )
    fresh = 1 if warheads.needs_fresh_lock(target) else memory.fresh
    return choice.warhead, dataclasses.replace(
        memory, shots=shots, fresh=fresh, held=warheads.Held.NONE
    )


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


def break_heading(heading: int, toward: int, state: int) -> tuple[int, int]:
    """Return the yaw a break holds, a quarter turn from the craft's heading
    away from the side its target lies on, and the dice state after it.

    ``toward`` is the yaw that points at the target. Only a target dead
    ahead, within DEAD_AHEAD of the heading, lets the dice pick the side.
    """
    if geometry.angle_between(toward, heading) <= DEAD_AHEAD:
        coin, state = dice.roll(state, 2)
        clockwise = coin == 1
    else:
        # Turn clockwise when the target lies to the left: more than a half
        # turn clockwise of the heading.
        clockwise = (toward - heading) % geometry.TURN > geometry.HALF_TURN
    turn = geometry.QUARTER_TURN if clockwise else -geometry.QUARTER_TURN
    return (heading + turn) % geometry.TURN, state


def escape(view: CraftView, first: Contact, second: Contact) -> tuple[int, int] | None:
    """Return the yaw and pitch that point away from two contacts alike,
    opposite the sum of the directions to them, each scaled to UNIT; None
    when that sum is under an eighth of UNIT, the craft nearly in line
    between them."""
    away = [0, 0, 0]
    for contact in (first, second):
        offset = tactics.offset(view, contact)
        reach = max(geometry.distance(*offset), 1)
        for axis in range(3):
            away[axis] -= offset[axis] * UNIT // reach
    if geometry.distance(*away) < UNIT // 8:
        return None
    return geometry.bearing(*away)


def break_holds(
    view: CraftView, question: Question, book: notebook.Notebook, memory: Memory
) -> bool:
    """Return whether a break goes on: its target still shown, the craft still
    inside the target's clearance, for less than BREAK_LIMIT_TICKS."""
    held = question.tick - book.phase_tick
    target = _remembered(question.contacts, memory)
    if target is None:
        logger.debug(
            "attack.break_lost object=%d target=%d ticks=%d",
            view.object_index,
            memory.target,
            held,
        )
        return False
    reach = geometry.distance(*tactics.offset(view, target))
    keep = tactics.clearance(target)
    if reach < keep and held < BREAK_LIMIT_TICKS:
        return True
    logger.debug(
        'attack.break_end object=%d target=%d reason="%s" ticks=%d distance=%d '
        "clearance=%d",
        view.object_index,
        target.object_index,
        "outside" if reach >= keep else "limit",
        held,
        reach,
        keep,
    )
    return False


def tail_threat(view: CraftView, question: Question) -> Contact | None:
    """Return the nearest enemy on the craft's tail that targets it, or None."""
    nearest = None
    for position, contact in enumerate(question.contacts):
        if contact.target != view.object_index:
            continue
        if not tactics.can_fight(view, position, contact):
            continue
        if not _on_tail(view, contact):
            continue
        reach = geometry.distance(*tactics.offset(view, contact))
        if nearest is None or (reach, contact.object_index) < nearest[0]:
            nearest = ((reach, contact.object_index), contact)
    return None if nearest is None else nearest[1]


def pursuer(view: CraftView, question: Question) -> Contact | None:
    """Return the enemy after the craft, or None.

    An enemy is after the craft while it targets it, the engine marks it
    hostile, and it is a small craft. The one the craft's attack notebook
    names as its target stays the pursuer while it is after the craft, so a
    craft that turned on its attacker keeps fighting it; otherwise the
    pursuer is the nearest enemy after the craft that hit it last or sits on
    its tail.
    """
    own = tactics.own_contact(view, question)
    fought = _fought(view)
    nearest = None
    for position, contact in enumerate(question.contacts):
        if contact.target != view.object_index or tactics.is_bulky(contact):
            continue
        if not tactics.can_fight(view, position, contact):
            continue
        if (contact.object_index, contact.object_signature) == fought:
            return contact
        hit = own is not None and own.last_attacker == contact.object_index
        if not hit and not _on_tail(view, contact):
            continue
        reach = geometry.distance(*tactics.offset(view, contact))
        if nearest is None or (reach, contact.object_index) < nearest[0]:
            nearest = ((reach, contact.object_index), contact)
    return None if nearest is None else nearest[1]


def fights_back(view: CraftView, question: Question, chaser: Contact) -> bool:
    """Return whether the craft fights a chaser: it has cannons, it can move,
    and its hull is at FIGHT_BACK_HULL of its strength or more. A craft whose
    own contact is not shown, or whose hull strength is 0, counts as whole."""
    own = tactics.own_contact(view, question)
    strength = 0 if own is None else own.hull_strength
    left = strength if own is None else strength - own.hull_damage
    share, whole = FIGHT_BACK_HULL
    fights = can_dogfight(view) and (strength <= 0 or left * whole >= strength * share)
    logger.debug(
        "attack.pursued object=%d pursuer=%d hull_left=%d hull_strength=%d fights=%d",
        view.object_index,
        chaser.object_index,
        left,
        strength,
        int(fights),
    )
    return fights


def weaving(view: CraftView) -> bool:
    """Return whether the craft's notebook shows this behavior's weave under
    way."""
    try:
        book = notebook.decode(view.notebook)
    except notebook.NotebookError:
        return False
    return (
        book is not None
        and book.behavior == CODE
        and book.phase == EVADE
        and Memory.read(book.scratch).weave.flying
    )


def can_dogfight(view: CraftView) -> bool:
    """Return whether the craft can fly a dogfight: it has cannons and can
    move."""
    return view.cannon_group_count > 0 and view.full_speed != 0


def _on_tail(view: CraftView, contact: Contact) -> bool:
    """Whether a contact sits on the craft's tail: within TAIL_RANGE, more
    than TAIL_ANGLE off its nose."""
    dx, dy, dz = tactics.offset(view, contact)
    if geometry.distance(dx, dy, dz) >= TAIL_RANGE:
        return False
    yaw, _ = geometry.bearing(dx, dy, dz)
    return geometry.angle_between(yaw, view.yaw) > TAIL_ANGLE


def _fought(view: CraftView) -> tuple[int, int] | None:
    """The object index and signature of the target the craft's attack
    notebook names, or None when it has none or another behavior wrote it."""
    try:
        book = notebook.decode(view.notebook)
    except notebook.NotebookError:
        return None
    if book is None or book.behavior != CODE or book.phase == SEEK:
        return None
    memory = Memory.read(book.scratch)
    return memory.target, memory.signature


def _seek(
    view: CraftView,
    question: Question,
    book: notebook.Notebook,
    memory: Memory,
    interval: int | None,
) -> Decision:
    """Return the decision of a craft with no target, its memory holding only
    the weave's state: weave from an enemy on its tail, else hold fire on its
    course."""
    tick = question.tick
    threat = tail_threat(view, question)
    if threat is not None and weave.may_start(memory.weave, tick):
        started, move, state = weave.start(
            view, threat.object_index, memory.weave, book.dice, tick
        )
        book = dataclasses.replace(
            book.in_phase(EVADE, tick),
            dice=state,
            scratch=Memory(weave=started).write(),
        )
        actions = dataclasses.replace(_weave_actions(move, interval), target=NO_OBJECT)
        return Decision(book.encode(), actions)
    book = book if book.phase == SEEK else book.in_phase(SEEK, tick)
    actions = Actions(
        think_interval=interval, target=NO_OBJECT, lasers=(HOLD_FIRE, HOLD_FIRE)
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _choose_target(
    view: CraftView, targets: list[Contact], memory: Memory, tier: int, tick: int
) -> tuple[Contact | None, Memory]:
    """Return the target to fight, kept or new, and the memory naming it; with
    no target, a memory holding only the weave's state."""
    kept = _remembered(targets, memory)
    if kept is not None and tick < memory.look_again:
        return kept, memory
    if not targets:
        return None, Memory(weave=memory.weave)
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
        memory = dataclasses.replace(memory, shots=0, fresh=0, held=warheads.Held.NONE)
    look_again = min(tick + LOOK_AGAIN_TICKS[tier], _U32_MAX)
    memory = dataclasses.replace(
        memory,
        target=nearest.object_index,
        signature=nearest.object_signature,
        look_again=look_again,
    )
    return nearest, memory


def _remembered(contacts: t.Iterable[Contact], memory: Memory) -> Contact | None:
    """Return the contact the memory names, the same craft by its signature,
    or None when none is."""
    return next(
        (
            contact
            for contact in contacts
            if contact.object_index == memory.target
            and contact.object_signature == memory.signature
        ),
        None,
    )


def _evade_ordered(view: CraftView, question: Question) -> Decision:
    """Return the decision that takes up a player's evade command: the
    target dropped and a weave begun on the craft's course, flown to its
    last swerve (1997's evasive order dropped the target and flew an
    evasive maneuver)."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    memory = Memory.read(book.scratch)
    threat = tail_threat(view, question)
    chaser = NO_OBJECT if threat is None else threat.object_index
    started, move, state = weave.start(view, chaser, memory.weave, book.dice, tick)
    memory = Memory(weave=started, ordered=1)
    book = dataclasses.replace(
        book.in_phase(EVADE, tick), dice=state, scratch=memory.write()
    )
    interval = None if view.think_interval == FIGHT_INTERVAL else FIGHT_INTERVAL
    logger.info("attack.evade_ordered object=%d chaser=%d", view.object_index, chaser)
    actions = dataclasses.replace(
        _weave_actions(move, interval), target=NO_OBJECT, evaded=True
    )
    return Decision(book.encode(), actions)


def _ordered_on(memory: Memory, tick: int) -> bool:
    """Return whether the memory's weave flies an evade command with a
    swerve still to fly."""
    flown = memory.weave
    return bool(memory.ordered) and (
        tick < flown.swerve_ends or flown.swerves < weave.SWERVES_MAX
    )


def _clear_of_all(view: CraftView, question: Question, move: weave.Move) -> weave.Move:
    """Return the weave's move with its yaw turned clear of the bulky craft in
    its way, the craft's own target among them: a weave aims at nothing, and
    avoidance skips the target an attack aims at."""
    steer = move.steer
    yaw = avoidance.clear_yaw(view, question, steer.yaw, steer.pitch, NO_OBJECT)
    return dataclasses.replace(move, steer=Steer(yaw, steer.pitch, steer.roll))


def _weave_actions(move: weave.Move, interval: int | None) -> Actions:
    """Return the actions of a weaving craft: the weave's move at full
    throttle, its cannons cold."""
    return Actions(
        steer=move.steer,
        throttle=FULL_THROTTLE,
        think_interval=interval,
        lasers=(HOLD_FIRE, HOLD_FIRE),
        countermeasure=move.flare,
    )


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
    """Return the decision that starts a break."""
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

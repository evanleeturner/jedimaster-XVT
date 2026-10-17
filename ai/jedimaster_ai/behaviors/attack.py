"""Attack: the dogfight a craft flies for the Attack order.

Purpose:
    A craft with the Attack order picks the nearest craft or static its order
    names, measured to its nearest part (``hull.nearest``), turns toward it, closes, and fires its cannons while the target sits
    inside its aim cone and firing range. Inside the target's keep
    (``hull.keep``: within KEEP of a starship's or platform's hull, else
    inside the target's clearance, ``tactics.clearance``, which grows with
    its size), with the target ahead, or anywhere when the target is a
    freighter, starship or platform, it breaks off a quarter turn to the
    side away from the target's nearest part (``hull.nearest``), or, when
    another bulky craft lies in that heading's way
    (``avoidance.in_the_way``), away from the two of them alike; it holds
    that heading until it is outside the keep, and comes round again; with
    an enemy on its tail that targets it, it weaves (``weave``) until the
    tail is clear or the weave's swerves are flown, steering clear of every
    bulky craft its own target among them, then fights again. Skill
    sets the aim cone, the firing range and the burst (``cannons``) and how
    soon it looks again for a nearer target. Its ion cannons hold, as in
    1997: only the Disable order fires them, at its target
    (``cannons.bursts``), so a craft whose only cannons are ion cannons
    fires no cannon here. Lined up or not,
    it fires a guided warhead at its target when ``warheads.choose`` lets it:
    its lock declared, a warhead that suits the target, inside 1997's limits.
    At skill level Jedi and up it may fire one unguided at a big ship, and a
    Sith Lord at any craft (``lord``); while only the aim holds that shot it
    steers at the point where the shot would meet its target, and its
    cannons still fire only lined up on it.

    With no target its order names in the battle, a craft an enemy is after
    (``pursuer``) fights that enemy while it may fight back (``fights_back``:
    it has cannons, it can move, and its hull is at FIGHT_BACK_HULL or more,
    or it is a Sith Lord, which fights at any hull: ``lord``),
    and otherwise weaves when the enemy sits on its tail (rulings 34 and 43).
    With neither, it flies 1997's cruise (``seek``): round its group's
    waypoints, else to its group's start point, else on its course.
    ``fight`` is the dogfight itself, at the targets a behavior gives it:
    ``home`` flies it for a craft going home that turns on its attacker, and
    for a craft with no way out (rulings 31 and 33), never cruising.

    A player's commands come first, as the 1997 target orders took them:
    the craft an attack or cover order named is the target while the craft
    may fight it, and the one an ignore order named is never picked
    (``tactics.commanded``). An evade command drops the target and starts
    a weave that flies all its swerves, chased or not; the ``evaded`` action
    tells the engine it was taken up.

    The rules' shape follows Starshatter Open's fighter pilot (target choice,
    fire control, breaking off), written new in this program's integer
    units; no Starshatter code is copied. The firing numbers are the game's
    own: the 1997 pilot's ranges, cone and bursts (``cannons``).

Phases, the notebook's phase byte:

    SEEK    no target: hold fire, cruise when the caller says (``seek``),
            else keep course; look again each think; an enemy on the tail
            starts a weave
    ATTACK  steer at the target, fire when lined up; each entry to ATTACK,
            and each new target, starts an attack run
    BREAK   inside the target's keep, ahead, or anywhere for a bulky
            target: hold a heading a quarter turn away from the side of the
            target's nearest part, or away from it and the bulky craft in
            that way, until outside the keep, for BREAK_LIMIT_TICKS at most
    EVADE   an enemy on the tail: weave

The scratch bytes, little-endian: u16 target index, u16 its signature,
u16 yaw and u16 pitch a break holds, u32 the tick to look again for a
nearer target, then the weave's state (``weave.STATE_BYTES``), then u8 1
while the weave flies an evade command, else 0; then u8 the warheads fired
in this attack run, u8 1 while a shot at a starfighter or transport waits
for the lock to lapse, else 0, u8 the last reason the craft held its
warheads (``warheads.Held``), and u8 the launcher a look asked a warhead of
plus 1 until the next look settles it (0 none); then the route ``seek`` flies
(``patrol.ROUTE_BYTES``); the rest zero. In SEEK only the weave's and the
route's bytes are kept, so a weave's rest outlasts the search and the
route outlasts every phase.

Invariants:
    - The decision follows from the view, the question and the notebook;
      random choices come from the craft's dice in its notebook.
    - It names only what the question showed: its target is a contact, and
      it fires only the cannon groups the view counts.
    - Ion cannons fire only at a Disable order's target, and there no other
      cannon does.
    - It targets only craft the engine marks hostile (``tactics.can_fight``).
    - Only a small craft is ever after a craft: a freighter, a starship or a
      platform shooting at it is no pursuer (ruling 42). A disabled craft is
      after nothing: with none of its subsystems working it can neither fly
      nor fire.
    - A break turns away from the side its target is on, so the turn never
      swings the nose across the target; the dice pick the side only when
      the target is dead ahead. A break another bulky craft's clearance
      blocks heads away from both instead, unless the craft lies nearly in
      line between them (``escape``) or the turn there swings the nose
      across either; then it keeps the quarter turn, and avoidance turns it
      round the other.
    - Unless it weaves, a craft inside a bulky target's keep breaks
      off from it rather than steer at it; a weave steers clear of it, and
      avoidance turns a break clear of it as of any bulky craft.
    - A fighting craft thinks every FIGHT_INTERVAL ticks, a Sith Lord twice
      as often (``FIGHT_INTERVALS``).
"""

from __future__ import annotations

import dataclasses
import logging
import struct
import typing as t

from .. import cannons
from .. import dice
from .. import geometry
from .. import hull
from .. import lord
from .. import notebook
from .. import tactics
from .. import warheads
from .. import weave
from ..world import Actions
from ..world import Commands
from ..world import Contact
from ..world import CraftView
from ..world import HOLD_FIRE
from ..world import Lock
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import avoidance
from . import BehaviorCode
from . import Decision
from . import patrol
from . import seek

logger = logging.getLogger(__name__)

CODE = BehaviorCode.ATTACK
SEEK = 0
ATTACK = 1
BREAK = 2
EVADE = 3

# Ticks between a fighting craft's thinks, by skill tier, 0 to 4: 64 ms, and
# a Sith Lord's 32 ms.
FIGHT_INTERVAL = 16
FIGHT_INTERVALS = (FIGHT_INTERVAL, FIGHT_INTERVAL, FIGHT_INTERVAL, FIGHT_INTERVAL, 8)
FULL_THROTTLE = 0xFFFF
# Two thirds, to stay behind a target it has caught.
CLOSE_THROTTLE = 0xAAAA
# Tier 0 flies at seven tenths at most while it attacks.
ROOKIE_THROTTLE = 0xB333

# By skill tier, 0 to 4: how soon a craft looks again for a nearer target.
LOOK_AGAIN_TICKS = (1500, 750, 250, 125, 64)

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
# A Sith craft on a starfighter target's tail, within this range and more
# than TAIL_ANGLE off the target's nose, flies at a third of full throttle,
# where a player's craft turns fastest (the AI guide's "Throttle and turning,
# measured"), and throttles up again once the target breaks away.
TAIL_TRICK_RANGE = 0x4000
TAIL_TRICK_THROTTLE = 0x5555
# A craft an enemy is after fights back while its hull, the hull strength less
# the hull damage, is at least this share of the strength (ruling 34: three
# quarters); shields never count.
FIGHT_BACK_HULL = (3, 4)

_SCRATCH = struct.Struct("<HHHHI")
_ORDERED = struct.Struct("<B")
_WARHEADS = struct.Struct("<BBBB")
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
    pending: int = 0
    # patrol imports this module, so its Route is made at call time.
    route: patrol.Route = dataclasses.field(default_factory=lambda: patrol.Route())

    @classmethod
    def read(cls, scratch: bytes) -> Memory:
        """Return the memory scratch bytes hold."""
        ordered_at = _SCRATCH.size + weave.STATE_BYTES
        warheads_at = ordered_at + _ORDERED.size
        return cls(
            *_SCRATCH.unpack_from(scratch),
            weave.Weave.read(scratch[_SCRATCH.size :]),
            *_ORDERED.unpack_from(scratch, ordered_at),
            *_WARHEADS.unpack_from(scratch, warheads_at),
            patrol.Route.read(scratch[warheads_at + _WARHEADS.size :]),
        )

    def write(self) -> bytes:
        """Return the memory as scratch bytes."""
        packed = _SCRATCH.pack(
            self.target, self.signature, self.yaw, self.pitch, self.look_again
        )
        packed += self.weave.write() + _ORDERED.pack(self.ordered)
        packed += _WARHEADS.pack(self.shots, self.fresh, self.held, self.pending)
        packed += self.route.write()
        return packed + bytes(notebook.SCRATCH_BYTES - len(packed))


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move in its dogfight: at the targets its order
    names, or, with none in the battle, at the enemy after it while it may
    fight back; a player's commands first."""
    if view.commands & Commands.EVADE:
        return _evade_ordered(view, question)
    named = tactics.order_targets(view, question)
    named += tactics.order_statics(view, question)
    targets = tactics.commanded(view, question, named)
    if not targets:
        chaser = pursuer(view, question)
        if chaser is not None and fights_back(view, question, chaser):
            targets = [chaser]
    return fight(view, question, targets, cruising=True)


def fight(
    view: CraftView,
    question: Question,
    targets: list[Contact],
    disabling: bool = False,
    cruising: bool = False,
) -> Decision:
    """Return the craft's next move in a dogfight at the nearest of targets,
    each a contact it may fight; ``disabling`` sets the Disable order's
    cannons (``cannons.bursts``) and warhead limit
    (``warheads.outlasts_disabling``); ``cruising`` flies ``seek``'s cruise
    while there is no target, else the craft keeps its course."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    memory = Memory.read(book.scratch)
    if book.phase == SEEK:
        memory = Memory(weave=memory.weave, route=memory.route)
    if memory.pending:
        memory = _settle(view, question, memory)
        book = dataclasses.replace(book, scratch=memory.write())
    if memory.fresh and view.lock_state != Lock.DECLARED:
        memory = dataclasses.replace(memory, fresh=0)
    tier = tactics.skill_tier(view)
    interval = fight_interval(view)

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
        return _seek(view, question, book, memory, interval, cruising)

    aim = cannons.aim(view, target, tier, disabling)
    keep = hull.keep(target)
    if aim.reach < keep and (aim.off_nose < BREAK_CONE or tactics.is_bulky(target)):
        held_yaw, held_pitch, blocked, state = _break_course(
            view, question, target, hull.nearest(view, target), book.dice
        )
        logger.debug(
            "attack.break object=%d target=%d distance=%d clearance=%d yaw=%d "
            "pitch=%d blocked=%d",
            view.object_index,
            target.object_index,
            aim.reach,
            keep,
            held_yaw,
            held_pitch,
            blocked,
        )
        memory = dataclasses.replace(memory, yaw=held_yaw, pitch=held_pitch)
        return _hold(view, book, state, BREAK, tick, memory, target, interval)

    threat = tail_threat(view, question)
    if threat is not None and not aim.lined_up and weave.may_start(memory.weave, tick):
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
    choice, memory = _warhead(view, question, target, memory, disabling)
    yaw, pitch = aim.yaw, aim.pitch
    if choice.lead is not None:
        yaw, pitch = geometry.bearing(choice.lead.dx, choice.lead.dy, choice.lead.dz)
    caught = (
        aim.reach < cannons.ALL_CANNONS_RANGE and aim.off_nose < cannons.AIM_CONE[tier]
    )
    throttle = CLOSE_THROTTLE if caught else FULL_THROTTLE
    if tier == 0:
        throttle = min(throttle, ROOKIE_THROTTLE)
    if _tail_trick(view, target, aim, tier):
        throttle = TAIL_TRICK_THROTTLE
    actions = Actions(
        steer=Steer(yaw, pitch, view.roll),
        throttle=throttle,
        think_interval=interval,
        target=target.object_index,
        lasers=(
            cannons.bursts(view, aim.reach, tier, disabling)
            if aim.lined_up
            else (HOLD_FIRE, HOLD_FIRE)
        ),
        warhead=choice.warhead,
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _warhead(
    view: CraftView,
    question: Question,
    target: Contact,
    memory: Memory,
    disabling: bool,
) -> tuple[warheads.Choice, Memory]:
    """Return the craft's warhead choice at its target and the memory
    counting the shot; the reason it holds is logged when it changes."""
    if view.warhead_launcher_count == 0:
        return warheads.Choice(None, warheads.Held.NONE), memory
    choice = warheads.choose(
        view, question, target, memory.shots, bool(memory.fresh), disabling
    )
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
        return choice, dataclasses.replace(memory, held=choice.held)
    launcher = choice.warhead.launcher
    shots = memory.shots + 1
    if choice.lead is None:
        logger.info(
            "attack.warhead object=%d target=%d launcher=%d paired=%d type=%d "
            "lock=%d coming=%d shots=%d",
            view.object_index,
            target.object_index,
            launcher,
            choice.warhead.paired,
            view.warhead_type[launcher],
            view.lock_ticks,
            target.warheads_coming,
            shots,
        )
    else:
        logger.info(
            "attack.unguided object=%d target=%d launcher=%d paired=%d type=%d "
            "lock=%d ticks=%d miss=%d shots=%d",
            view.object_index,
            target.object_index,
            launcher,
            choice.warhead.paired,
            view.warhead_type[launcher],
            view.lock_ticks,
            choice.lead.ticks,
            choice.lead.miss,
            shots,
        )
    return choice, dataclasses.replace(
        memory, pending=launcher + 1, held=warheads.Held.NONE
    )


def _settle(view: CraftView, question: Question, memory: Memory) -> Memory:
    """Return the memory with the shot the last look asked for settled: made,
    when the launcher's wait is running, counted and its fresh-lock flag set;
    else dropped, with a line, so the craft may ask again."""
    launcher = memory.pending - 1
    target = _remembered(question.contacts, memory)
    if not warheads.made(view, launcher):
        logger.debug(
            "attack.warhead_unmade object=%d target=%d launcher=%d",
            view.object_index,
            memory.target,
            launcher,
        )
        return dataclasses.replace(memory, pending=0)
    fresh = memory.fresh
    if target is not None and warheads.needs_fresh_lock(target):
        fresh = 1
    return dataclasses.replace(memory, shots=memory.shots + 1, fresh=fresh, pending=0)


def _break_course(
    view: CraftView,
    question: Question,
    target: Contact,
    near: hull.Nearest,
    state: int,
) -> tuple[int, int, int, int]:
    """Return the yaw and pitch a break from the target holds, the object
    index of the bulky craft that turned it (NO_OBJECT when none did), and
    the dice state after it: a quarter turn away from the side of the
    target's nearest part, ``near`` (``break_heading``), or, with a bulky
    craft in that turn's way, away from the two alike (``escape``), unless
    the craft lies nearly in line between them or that heading swings its
    nose across either.
    """
    toward = geometry.bearing(near.dx, near.dy, near.dz)[0]
    held_yaw, state = break_heading(view.yaw, toward, state)
    obstacle = avoidance.in_the_way(
        view, question, held_yaw, geometry.LEVEL, target.object_index
    )
    away = (
        None
        if obstacle is None
        else escape((near.dx, near.dy, near.dz), obstacle.offset)
    )
    if away is None or any(
        geometry.sweeps(view.yaw, away[0], across) for across in (toward, obstacle.yaw)
    ):
        return held_yaw, geometry.LEVEL, NO_OBJECT, state
    return away[0], away[1], obstacle.contact.object_index, state


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


def escape(
    first: tuple[int, int, int], second: tuple[int, int, int]
) -> tuple[int, int] | None:
    """Return the yaw and pitch that point away from two things alike, each
    given by its offset from the craft: opposite the sum of the directions
    to them, each scaled to UNIT; None when that sum is under an eighth of
    UNIT, the craft nearly in line between them."""
    away = [0, 0, 0]
    for offset in (first, second):
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
    inside the target's keep (``hull.keep``), for less than BREAK_LIMIT_TICKS."""
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
    reach = hull.nearest(view, target).distance
    keep = hull.keep(target)
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


def fight_interval(view: CraftView) -> int | None:
    """Return the think interval a fighting craft asks for, FIGHT_INTERVALS
    by its skill tier, or None when it thinks at that already."""
    wanted = FIGHT_INTERVALS[tactics.skill_tier(view)]
    return None if view.think_interval == wanted else wanted


def tail_threat(view: CraftView, question: Question) -> Contact | None:
    """Return the nearest enemy on the craft's tail that targets it, or None;
    a disabled craft is never one."""
    nearest = None
    for position, contact in enumerate(question.contacts):
        if contact.target != view.object_index or contact.disabled:
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
    hostile, it is a small craft, and it is not disabled (``Contact.disabled``).
    The one the craft's attack notebook names as its target stays the
    pursuer while it is after the craft, so a craft that turned on its
    attacker keeps fighting it; otherwise the pursuer is the nearest enemy
    after the craft that hit it last or sits on its tail.
    """
    own = tactics.own_contact(view, question)
    fought = _fought(view)
    nearest = None
    for position, contact in enumerate(question.contacts):
        if contact.target != view.object_index or tactics.is_bulky(contact):
            continue
        if contact.disabled or not tactics.can_fight(view, position, contact):
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
    and its hull is at FIGHT_BACK_HULL of its strength or more, or it is a
    Sith Lord (``lord.is_lord``), which never retreats. A craft whose own
    contact is not shown, or whose hull strength is 0, counts as whole."""
    own = tactics.own_contact(view, question)
    strength = 0 if own is None else own.hull_strength
    left = strength if own is None else strength - own.hull_damage
    share, whole = FIGHT_BACK_HULL
    fights = can_dogfight(view) and (
        lord.is_lord(view) or strength <= 0 or left * whole >= strength * share
    )
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


def _tail_trick(view: CraftView, target: Contact, aim: cannons.Aim, tier: int) -> bool:
    """Whether a Sith craft flies the tail trick, logged when it does: on the
    tail of a starfighter target that is not disabled, within
    TAIL_TRICK_RANGE and more than TAIL_ANGLE off the target's nose."""
    if tier < tactics.SITH_APPRENTICE_TIER:
        return False
    if target.genus != tactics.GENUS_STARFIGHTER or target.disabled:
        return False
    if aim.reach >= TAIL_TRICK_RANGE:
        return False
    dx, dy, dz = tactics.offset(view, target)
    yaw, _ = geometry.bearing(-dx, -dy, -dz)
    behind = geometry.angle_between(yaw, target.yaw)
    if behind <= TAIL_ANGLE:
        return False
    logger.debug(
        "attack.tail_trick object=%d target=%d distance=%d angle=%d",
        view.object_index,
        target.object_index,
        aim.reach,
        behind,
    )
    return True


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
    cruising: bool,
) -> Decision:
    """Return the decision of a craft with no target, its memory holding only
    the weave's state and the route: weave from an enemy on its tail, else
    hold fire on ``seek``'s cruise when cruising, on its course when not."""
    tick = question.tick
    threat = tail_threat(view, question)
    if threat is not None and weave.may_start(memory.weave, tick):
        started, move, state = weave.start(
            view, threat.object_index, memory.weave, book.dice, tick
        )
        book = dataclasses.replace(
            book.in_phase(EVADE, tick),
            dice=state,
            scratch=Memory(weave=started, route=memory.route).write(),
        )
        actions = dataclasses.replace(_weave_actions(move, interval), target=NO_OBJECT)
        return Decision(book.encode(), actions)
    book = book if book.phase == SEEK else book.in_phase(SEEK, tick)
    moved = Actions()
    if cruising:
        route, moved = seek.fly(view, question, memory.route)
        memory = dataclasses.replace(memory, route=route)
    actions = dataclasses.replace(
        moved, think_interval=interval, target=NO_OBJECT, lasers=(HOLD_FIRE, HOLD_FIRE)
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _choose_target(
    view: CraftView, targets: list[Contact], memory: Memory, tier: int, tick: int
) -> tuple[Contact | None, Memory]:
    """Return the target to fight, kept or new, and the memory naming it; with
    no target, a memory holding only the weave's state. The nearest is
    measured to each target's nearest part (``hull.nearest``), so a craft
    beside a big ship's flank takes it before a craft whose position is
    nearer (ruling 93)."""
    kept = _remembered(targets, memory)
    if kept is not None and tick < memory.look_again:
        return kept, memory
    if not targets:
        return None, Memory(weave=memory.weave, route=memory.route)
    reach, nearest = min(
        ((hull.nearest(view, contact).distance, contact) for contact in targets),
        key=lambda measured: (measured[0], measured[1].object_index),
    )
    if kept is None or nearest.object_index != kept.object_index:
        logger.info(
            "attack.target object=%d target=%d distance=%d",
            view.object_index,
            nearest.object_index,
            reach,
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
    memory = Memory(weave=started, ordered=1, route=memory.route)
    book = dataclasses.replace(
        book.in_phase(EVADE, tick), dice=state, scratch=memory.write()
    )
    interval = fight_interval(view)
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
    """Return the weave's move with its heading turned clear of the bulky
    craft in its way, the craft's own target among them: a weave aims at
    nothing, and avoidance skips the target an attack aims at."""
    steer = move.steer
    yaw, pitch = avoidance.clear_heading(
        view, question, steer.yaw, steer.pitch, NO_OBJECT
    )
    return dataclasses.replace(move, steer=Steer(yaw, pitch, steer.roll))


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

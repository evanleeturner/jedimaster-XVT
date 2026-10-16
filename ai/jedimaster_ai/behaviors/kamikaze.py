"""Kamikaze (order 37): dive at the order's target at full throttle and ram it.

Purpose:
    A craft with the Kamikaze order picks the nearest craft its order names
    that it may fight, a player's commands applied (``tactics.commanded``),
    and dives at it: it steers straight at the target's position at full
    throttle and never breaks off. It keeps that target until the target
    leaves the list (destroyed, its slot taken by another craft, or named
    by an ignore command) or a player's attack command names another, as
    1997's dive kept it. While it closes it fires as Attack fires: its
    cannons, the ion cannons held, while the target sits inside Attack's
    aim cone and firing range (``cannons.AIM_CONE``, ``cannons.fire_range``,
    ``cannons.bursts``), and a guided warhead whenever
    ``warheads.choose`` lets it (ruling K1). Avoidance leaves the target
    alone, since the dive steers into the target's clearance, and turns the
    craft round any other bulky craft in its way (ruling K3). The engine's
    collisions do the rest: a computer-flown freighter, starship or platform
    that a craft hits takes damage, and so does the craft; every retail
    Kamikaze target is one.

    With no target, it flies its group's waypoints as Patrol does
    (``patrol.advance``, ``patrol.cruise``) and weaves while an enemy sits on
    its tail (``patrol.evade``), as Circle and Evade does (ruling K2). A
    weave under way is flown out before a dive begins.

    An evade command drops the target and starts a weave on the craft's
    course, flown to its last swerve, chased or not, as Attack flies one;
    then the craft cruises, and dives again.

    The blueprint is 1997's three kamikaze plans, deleted with the 1997 AI:
    kamikaze1pln cruised the waypoints, dodging attackers, and looked for
    the nearest order target; kamikaze2pln steered at the target at full
    throttle (``paiman_kamikazemaneuver``) and fired as a fighter shoots;
    kamikaze3pln flew the evade command. The order has no done test: the
    mission moves the craft on.

Phases, the notebook's phase byte:

    CRUISE  no target: the waypoints, and a weave while an enemy is on the
            tail
    DIVE    steer at the target at full throttle, fire when lined up
    EVADE   an evade command's weave

The scratch bytes, little-endian: the route (``patrol.ROUTE_BYTES``), the
weave's state (``weave.STATE_BYTES``), u16 the target's index and u16 its
signature, u8 the warheads fired in this dive, u8 1 while a shot at a
starfighter or transport waits for the lock to lapse, else 0, u8 1 while the
weave flies an evade command, else 0; the rest zero.

Invariants:
    - It never turns away from its target: no clearance breaks a dive.
    - Its ion cannons never fire.
    - A notebook written on another order slot starts the route again.
    - The decision follows from the view, the question and the notebook;
      random choices come from the craft's dice in its notebook.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import cannons
from .. import geometry
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
from ..world import Warhead
from . import attack
from . import BehaviorCode
from . import Decision
from . import patrol

logger = logging.getLogger(__name__)

CODE = BehaviorCode.KAMIKAZE
CRUISE = 0
DIVE = 1
EVADE = 2

_DIVE = struct.Struct("<HHBBB")


@dataclasses.dataclass(frozen=True)
class Memory:
    """The scratch bytes' fields."""

    route: patrol.Route = patrol.Route()
    weave: weave.Weave = weave.Weave()
    target: int = NO_OBJECT
    signature: int = 0
    shots: int = 0
    fresh: int = 0
    ordered: int = 0

    @classmethod
    def read(cls, scratch: bytes) -> Memory:
        """Return the memory scratch bytes hold."""
        dive_at = patrol.ROUTE_BYTES + weave.STATE_BYTES
        return cls(
            patrol.Route.read(scratch),
            weave.Weave.read(scratch[patrol.ROUTE_BYTES :]),
            *_DIVE.unpack_from(scratch, dive_at),
        )

    def write(self) -> bytes:
        """Return the memory as scratch bytes."""
        packed = self.route.write() + self.weave.write()
        packed += _DIVE.pack(
            self.target, self.signature, self.shots, self.fresh, self.ordered
        )
        return packed + bytes(notebook.SCRATCH_BYTES - len(packed))


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move: a dive at its target, else its cruise; a
    player's evade command first."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    memory = Memory.read(book.scratch)
    if memory.route.slot != view.order_slot:
        memory = Memory(route=patrol.Route(slot=view.order_slot))
    if view.commands & Commands.EVADE:
        return _evade_ordered(view, question, book, memory)
    if book.phase == EVADE and memory.weave.flying:
        chased = attack.tail_threat(view, question) is not None or _ordered_on(
            memory, tick
        )
        flown, move, state = weave.fly(view, memory.weave, chased, book.dice, tick)
        memory = dataclasses.replace(
            memory, weave=flown, ordered=memory.ordered if flown.flying else 0
        )
        book = dataclasses.replace(book, dice=state)
        if move is not None:
            return _weaving(view, book, memory, move)
    targets = tactics.commanded(view, question, tactics.order_targets(view, question))
    if targets and not memory.weave.flying:
        return _dive(view, question, book, memory, targets)
    return _cruise(view, question, book, memory, len(targets))


def _dive(
    view: CraftView,
    question: Question,
    book: notebook.Notebook,
    memory: Memory,
    targets: list[Contact],
) -> Decision:
    """Return the dive: steer at the target at full throttle, its cannons
    and warheads fired as Attack fires them."""
    tick = question.tick
    target = _kept(targets, memory) or _nearest(view, targets)
    dx, dy, dz = tactics.offset(view, target)
    reach = geometry.distance(dx, dy, dz)
    dived = (memory.target, memory.signature)
    if book.phase != DIVE or (target.object_index, target.object_signature) != dived:
        book = book.in_phase(DIVE, tick)
        memory = dataclasses.replace(
            memory,
            target=target.object_index,
            signature=target.object_signature,
            shots=0,
            fresh=0,
        )
        logger.info(
            "kamikaze.dive object=%d target=%d distance=%d genus=%d targets=%d",
            view.object_index,
            target.object_index,
            reach,
            target.genus,
            len(targets),
        )
    if memory.fresh and view.lock_state != Lock.DECLARED:
        memory = dataclasses.replace(memory, fresh=0)
    tier = tactics.skill_tier(view.skill)
    yaw, pitch = geometry.bearing(dx, dy, dz)
    off_nose = geometry.angle_between(yaw, view.yaw)
    off_level = geometry.angle_between(pitch, view.pitch)
    lined_up = (
        off_nose < cannons.AIM_CONE[tier]
        and off_level < cannons.AIM_CONE[tier]
        and reach < cannons.fire_range(view, target, tier)
    )
    shot, memory, held = _warhead(view, question, target, memory)
    logger.debug(
        "kamikaze.closing object=%d target=%d distance=%d off_nose=%d off_level=%d "
        'lined_up=%d held="%s" shots=%d',
        view.object_index,
        target.object_index,
        reach,
        off_nose,
        off_level,
        int(lined_up),
        held.name.lower(),
        memory.shots,
    )
    interval = _fight_interval(view)
    actions = Actions(
        steer=Steer(yaw, pitch, view.roll),
        throttle=tactics.FULL_THROTTLE,
        think_interval=interval,
        target=target.object_index,
        lasers=(
            cannons.bursts(view, reach, tier) if lined_up else (HOLD_FIRE, HOLD_FIRE)
        ),
        warhead=shot,
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _warhead(
    view: CraftView, question: Question, target: Contact, memory: Memory
) -> tuple[Warhead | None, Memory, warheads.Held]:
    """Return the guided warhead the craft fires at its target, None when it
    holds, the memory counting the shot, and why it holds."""
    choice = warheads.choose(view, question, target, memory.shots, bool(memory.fresh))
    if choice.warhead is None:
        return None, memory, choice.held
    launcher = choice.warhead.launcher
    shots = memory.shots + 1
    logger.info(
        "kamikaze.warhead object=%d target=%d launcher=%d type=%d lock=%d "
        "coming=%d shots=%d",
        view.object_index,
        target.object_index,
        launcher,
        view.warhead_type[launcher],
        view.lock_ticks,
        target.warheads_coming,
        shots,
    )
    fresh = 1 if warheads.needs_fresh_lock(target) else memory.fresh
    return (
        choice.warhead,
        dataclasses.replace(memory, shots=shots, fresh=fresh),
        choice.held,
    )


def _cruise(
    view: CraftView,
    question: Question,
    book: notebook.Notebook,
    memory: Memory,
    targets: int,
) -> Decision:
    """Return the cruise round the waypoints, weaving while an enemy sits on
    the craft's tail."""
    if book.phase != CRUISE:
        book = book.in_phase(CRUISE, question.tick)
        logger.debug(
            "kamikaze.cruise object=%d targets=%d weaving=%d waypoint=%d",
            view.object_index,
            targets,
            int(memory.weave.flying),
            memory.route.waypoint,
        )
    own = tactics.own_contact(view, question)
    genus = None if own is None else own.genus
    route = patrol.advance(view, memory.route, genus)
    actions = patrol.cruise(view, question, route, own)
    flown, actions, state = patrol.evade(
        view, question, memory.weave, actions, book.dice
    )
    memory = dataclasses.replace(
        memory, route=route, weave=flown, target=NO_OBJECT, signature=0
    )
    book = dataclasses.replace(book, dice=state, scratch=memory.write())
    return Decision(book.encode(), actions)


def _evade_ordered(
    view: CraftView, question: Question, book: notebook.Notebook, memory: Memory
) -> Decision:
    """Return the decision that takes up a player's evade command: the target
    dropped and a weave begun on the craft's course, flown to its last
    swerve (1997's kamikaze3pln)."""
    tick = question.tick
    threat = attack.tail_threat(view, question)
    chaser = NO_OBJECT if threat is None else threat.object_index
    started, move, state = weave.start(view, chaser, memory.weave, book.dice, tick)
    memory = dataclasses.replace(
        memory, weave=started, ordered=1, target=NO_OBJECT, signature=0
    )
    book = dataclasses.replace(book.in_phase(EVADE, tick), dice=state)
    logger.info("kamikaze.evade_ordered object=%d chaser=%d", view.object_index, chaser)
    decision = _weaving(view, book, memory, move)
    actions = dataclasses.replace(decision.actions, evaded=True)
    return dataclasses.replace(decision, actions=actions)


def _weaving(
    view: CraftView, book: notebook.Notebook, memory: Memory, move: weave.Move
) -> Decision:
    """Return the evade command's weave: the move at full throttle, no
    target, the cannons cold."""
    actions = Actions(
        steer=move.steer,
        throttle=tactics.FULL_THROTTLE,
        think_interval=_fight_interval(view),
        target=NO_OBJECT,
        lasers=(HOLD_FIRE, HOLD_FIRE),
        countermeasure=move.flare,
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _ordered_on(memory: Memory, tick: int) -> bool:
    """Return whether the weave flies an evade command with a swerve still to
    fly."""
    flown = memory.weave
    return bool(memory.ordered) and (
        tick < flown.swerve_ends or flown.swerves < weave.SWERVES_MAX
    )


def _kept(targets: list[Contact], memory: Memory) -> Contact | None:
    """Return the target the dive holds, when it is still among targets."""
    for contact in targets:
        if (
            contact.object_index == memory.target
            and contact.object_signature == memory.signature
        ):
            return contact
    return None


def _nearest(view: CraftView, targets: list[Contact]) -> Contact:
    """Return the nearest of targets, the lower index between two as near."""
    return min(
        targets,
        key=lambda contact: (
            geometry.distance(*tactics.offset(view, contact)),
            contact.object_index,
        ),
    )


def _fight_interval(view: CraftView) -> int | None:
    """Return the think interval to ask for: Attack's, unless the craft
    thinks at it already."""
    if view.think_interval == attack.FIGHT_INTERVAL:
        return None
    return attack.FIGHT_INTERVAL

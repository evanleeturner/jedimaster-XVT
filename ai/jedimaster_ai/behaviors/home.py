"""Home: a craft leaves the battle, into its mothership's hangar or by hyperspace.

Purpose:
    The behavior of a craft its books send home (``progress``) and of the Go
    Home and SS Go Home orders: the 1997 fly-home, enter-hangar and
    into-hyperspace plans (``AI5_WAVE2_1997.md`` sections 3 and 4). Each
    think the craft takes its way out from the view:

    - by hyperspace when its books say it may jump: to its home point at
      full throttle, leaving by hyperspace, and within JUMP_RANGE of it, or
      at once with no home point, it departs
    - into its mothership when it has one: going home to the outside hangar
      point, slowing to speed 150, 100 and 75 within 65,536, 32,768 and
      16,384 units; within HANGAR_TURN of it, entering the hangar, to the
      inside point at the mothership's speed plus 25 (40 under a mothership
      slower than 25), thinking every 29 ticks; it docks once its books say
      it is at the hangar
    - else, with no way out, it fights the nearest enemy in the battle when
      it can dogfight (hiding at a jump point is no way out); with
      none, it flies to its home point and stops there. A mothership that
      arrives for it, or a jump its books allow, takes it home again

    On the way to its jump or its hangar, an enemy after it (``attack.
    pursuer``: a small craft that targets it and hit it last or sits on its
    tail) turns it round to fight while it may fight back (``attack.
    fights_back``: its hull at three quarters or more), and
    otherwise it flies home evading, weaving about its way home whenever the
    enemy sits on its tail. A homing shot closing on it
    (``countermeasures.incoming``) makes it fly home evading too, weaving
    from the shot. Once the enemy is dead, gone, or targets another craft
    and no shot closes, it goes home again. At the door it goes on: a
    craft departing, turning in to the hangar or docking is not turned
    round.

    A craft on a starship's order (STARSHIP_ORDERS) goes home as the 1997
    starship plan did: by hyperspace, or, when its group leaves by
    mothership, it stops for good where it is, holding steady.

Invariants:
    - The craft targets its mothership while it flies to it, so
      ``avoidance`` never turns it off its own carrier; a craft entering the
      hangar passes the carrier's hull (``collide.c``).
    - It departs only when its books say it may jump and docks only when
      they say it is at the hangar: the engine takes every answer.
    - A craft already entering hyperspace is sent nothing; the engine flies
      it out.
    - The way out is read from the view each think. A fight is attack's,
      with attack's notebook; a weave home keeps its state in a home
      notebook (BehaviorCode.HOME, the weave's STATE_BYTES first in the
      scratch), which ends a weave under way once no enemy is after the
      craft and no shot closes on it. Otherwise the notebook goes back as
      it came. A new route is logged at DEBUG when the activity changes: a
      fighting craft's is NONE.
"""

from __future__ import annotations

import dataclasses
import logging
import typing as t

from .. import geometry
from .. import notebook
from .. import tactics
from .. import weave
from ..world import Actions
from ..world import Activity
from ..world import Books
from ..world import Contact
from ..world import CraftView
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import attack
from . import BehaviorCode
from . import countermeasures
from . import Decision
from . import formation

logger = logging.getLogger(__name__)

CODE = BehaviorCode.HOME
FULL_THROTTLE = 0xFFFF
# Within 16,384 units of its home point a craft leaves by hyperspace.
JUMP_RANGE = 0x4000
# Within 2,048 units of the outside hangar point a craft turns in.
HANGAR_TURN = 0x800
# The speed a craft slows to within each distance of the outside hangar
# point, nearest first; farther, full throttle.
APPROACH = ((0x4000, 75), (0x8000, 100), (0x10000, 150))
# Into the hangar: the mothership's speed plus ENTER_MARGIN, or ENTER_FLOOR
# under a mothership slower than SLOW_MOTHERSHIP.
ENTER_MARGIN = 25
ENTER_FLOOR = 40
SLOW_MOTHERSHIP = 25
# Ticks between thinks while entering the hangar.
ENTER_INTERVAL = 29
OBJECT_KIND_ENTERING_HYPERSPACE = 5
# A follower whose leader has begun its jump departs at once when the
# leader's speed reaches LAST_CALL, half the 3,600 at which the engine takes
# a departing craft out: a leader took about 2,000 ticks from its jump to
# here, a bulky follower's 34 rolls of 1 in JUMP_ODDS.
LAST_CALL = 1800
# The starship orders, SS Patrol Loop (21) to SS Patrol and Disable (27), and
# SS Go Home (29): their 1997 plans went home by the starship's rule.
STARSHIP_ORDERS = frozenset((21, 22, 23, 24, 25, 26, 27, 29))


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next step out of the battle."""
    own = tactics.own_contact(view, question)
    if own is not None and own.object_kind == OBJECT_KIND_ENTERING_HYPERSPACE:
        return Decision(view.notebook)
    if view.order_code in STARSHIP_ORDERS:
        route, decision = _starship(view, question)
    elif view.books & Books.MAY_JUMP:
        route, decision = _on_the_way(view, question, "jump", _jump(view, question))
    elif view.mothership != NO_OBJECT:
        route, decision = _on_the_way(view, question, *_hangar(view, question))
    else:
        route, decision = _stranded(view, question)
    activity = decision.actions.activity
    if activity is not None and activity != view.activity:
        logger.debug(
            'home.route object=%d route="%s" from=%d books=%d',
            view.object_index,
            route,
            view.activity,
            view.books,
        )
    return decision


def _starship(view: CraftView, question: Question) -> tuple[str, Decision]:
    """Go home by the 1997 starship plan: stop for good when the group leaves
    by mothership, else jump, or wait with no jump allowed."""
    if view.books & Books.BY_MOTHERSHIP:
        actions = Actions(throttle=0, activity=Activity.NONE, target=NO_OBJECT)
        return "stop", Decision(view.notebook, actions)
    if view.books & Books.MAY_JUMP:
        return "jump", Decision(view.notebook, _jump(view, question))
    return "wait", Decision(view.notebook, _wait(view, question))


def _on_the_way(
    view: CraftView, question: Question, route: str, actions: Actions
) -> tuple[str, Decision]:
    """Fly a route out, unless an enemy is after the craft on the way to its
    jump or its hangar: then fight it while the craft may fight back, else
    fly the route evading; a homing shot closing on it with no enemy after
    it flies the route evading too."""
    if route not in ("jump", "hangar") or actions.depart:
        return route, Decision(view.notebook, actions)
    chaser = attack.pursuer(view, question)
    if chaser is None:
        if countermeasures.incoming(view, question) is None:
            return route, _clear(view, question, actions)
        return "evade", _evade(view, question, actions)
    if attack.fights_back(view, question, chaser):
        return "fight", _fighting(attack.fight(view, question, [chaser]))
    return "evade", _evade(view, question, actions)


def _stranded(view: CraftView, question: Question) -> tuple[str, Decision]:
    """With no way out, fight the nearest enemy when the craft can dogfight;
    else wait at the home point."""
    enemies = tactics.enemies(view, question)
    if enemies and attack.can_dogfight(view):
        return "stranded", _fighting(attack.fight(view, question, enemies))
    return "wait", Decision(view.notebook, _wait(view, question))


def _fighting(decision: Decision) -> Decision:
    """Return attack's decision for a craft no longer going home: its
    activity NONE."""
    actions = dataclasses.replace(decision.actions, activity=Activity.NONE)
    return dataclasses.replace(decision, actions=actions)


def _evade(view: CraftView, question: Question, actions: Actions) -> Decision:
    """Fly the route's actions going home evading, weaving about the route's
    heading while an enemy sits on the craft's tail or a homing shot closes
    on it (``attack.chased_by``); each answer built from the weave's move is
    flagged ``weaving``."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    flown = weave.Weave.read(book.scratch)
    threat = attack.chased_by(view, question)
    move = None
    state = book.dice
    if flown.flying:
        flown, move, state = weave.fly(view, flown, threat is not None, state, tick)
    elif threat is not None and weave.may_start(flown, tick):
        course = None if actions.steer is None else actions.steer.yaw
        flown, move, state = weave.start(view, threat, flown, state, tick, course)
    book = dataclasses.replace(book, dice=state, scratch=_scratch(flown))
    actions = dataclasses.replace(actions, activity=Activity.GOING_HOME_EVADING)
    if move is not None:
        actions = dataclasses.replace(
            actions,
            steer=move.steer,
            throttle=FULL_THROTTLE,
            countermeasure=move.flare,
            weaving=True,
        )
    return Decision(book.encode(), actions)


def _clear(view: CraftView, question: Question, actions: Actions) -> Decision:
    """Fly the route's actions with no enemy after the craft, ending a weave
    its home notebook has under way: the tail is clear."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    flown = weave.Weave.read(book.scratch)
    if not flown.flying:
        return Decision(view.notebook, actions)
    flown, _, state = weave.fly(view, flown, False, book.dice, tick)
    book = dataclasses.replace(book, dice=state, scratch=_scratch(flown))
    return Decision(book.encode(), actions)


def _scratch(flown: weave.Weave) -> bytes:
    """Return a home notebook's scratch: the weave's state, then zeros."""
    packed = flown.write()
    return packed + bytes(notebook.SCRATCH_BYTES - len(packed))


def _jump(view: CraftView, question: Question) -> Actions:
    """Fly to the home point at full throttle, or eased while leading
    followers, and depart within JUMP_RANGE of it; with no home point,
    depart where it is. A follower whose leader jumps too flies in
    formation and, within JUMP_RANGE, departs when its roll says
    (``formation.jumps_now``). Once the leader has begun its jump, the
    follower departs on its roll wherever it is, and surely once the
    leader's speed reaches LAST_CALL: the flight leaves one by one behind
    its leader, none left to a place behind a leader gone."""
    point = view.home_point if view.books & Books.HOME_POINT else _here(view)
    dx, dy, dz = _offset(view, point)
    leader = formation.leader_for(view, question, _jumping)
    if leader is not None and leader.object_kind == OBJECT_KIND_ENTERING_HYPERSPACE:
        sure = leader.speed >= LAST_CALL
        if sure or formation.jumps_now(view, question.tick):
            logger.debug(
                "home.after_leader object=%d leader=%d speed=%d sure=%d",
                view.object_index,
                leader.object_index,
                leader.speed,
                sure,
            )
            return _depart()
        return _in_formation(view, question, leader, Activity.LEAVING_BY_HYPERSPACE)
    if geometry.distance(dx, dy, dz) < JUMP_RANGE and (
        leader is None or formation.jumps_now(view, question.tick)
    ):
        return _depart()
    if leader is not None:
        return _in_formation(view, question, leader, Activity.LEAVING_BY_HYPERSPACE)
    return Actions(
        steer=_toward(view, dx, dy, dz),
        throttle=formation.eased(view, question, FULL_THROTTLE),
        activity=Activity.LEAVING_BY_HYPERSPACE,
        target=NO_OBJECT,
    )


def _wait(view: CraftView, question: Question) -> Actions:
    """Fly home with no way out: to the home point, stopping within
    JUMP_RANGE of it; with no home point, stop where it is. A follower
    whose leader does the same flies in formation, and stops beside it."""
    leader = formation.leader_for(view, question, _going_home(NO_OBJECT))
    if leader is not None:
        return _in_formation(view, question, leader, Activity.GOING_HOME)
    point = view.home_point if view.books & Books.HOME_POINT else _here(view)
    dx, dy, dz = _offset(view, point)
    if geometry.distance(dx, dy, dz) < JUMP_RANGE:
        return Actions(throttle=0, activity=Activity.GOING_HOME, target=NO_OBJECT)
    return Actions(
        steer=_toward(view, dx, dy, dz),
        throttle=formation.eased(view, question, FULL_THROTTLE),
        activity=Activity.GOING_HOME,
        target=NO_OBJECT,
    )


def _hangar(view: CraftView, question: Question) -> tuple[str, Actions]:
    """Fly into the mothership: to the outside hangar point, then from
    HANGAR_TURN of it to the inside point, and dock there. Beyond the slow
    approach, a follower whose leader goes home to the same mothership
    flies in formation; nearer, each craft makes its own approach."""
    if view.books & Books.AT_HANGAR:
        return "dock", Actions(dock=True)
    mothership = next(
        (c for c in question.contacts if c.object_index == view.mothership), None
    )
    target = None if mothership is None else view.mothership
    dx, dy, dz = _offset(view, view.outside_hangar)
    reach = geometry.distance(dx, dy, dz)
    if view.activity == Activity.ENTERING_HANGAR or reach < HANGAR_TURN:
        speed = 0 if mothership is None else mothership.speed
        enter = ENTER_FLOOR if speed < SLOW_MOTHERSHIP else speed + ENTER_MARGIN
        return "enter", Actions(
            steer=_toward(view, *_offset(view, view.inside_hangar)),
            throttle=tactics.throttle_for(view, enter),
            activity=Activity.ENTERING_HANGAR,
            think_interval=ENTER_INTERVAL,
            target=target,
        )
    slow = next((speed for within, speed in APPROACH if reach < within), None)
    if slow is None:
        leader = formation.leader_for(view, question, _going_home(view.mothership))
        if leader is not None:
            actions = _in_formation(view, question, leader, Activity.GOING_HOME)
            return "hangar", dataclasses.replace(actions, target=target)
    return "hangar", Actions(
        steer=_toward(view, dx, dy, dz),
        throttle=formation.eased(view, question, FULL_THROTTLE)
        if slow is None
        else tactics.throttle_for(view, slow),
        activity=Activity.GOING_HOME,
        target=target,
    )


def _depart() -> Actions:
    """Return the jump out: full throttle, leaving by hyperspace, no
    target."""
    return Actions(
        throttle=FULL_THROTTLE,
        activity=Activity.LEAVING_BY_HYPERSPACE,
        target=NO_OBJECT,
        depart=True,
    )


def _jumping(leader: Contact) -> bool:
    return leader.activity == Activity.LEAVING_BY_HYPERSPACE


def _going_home(mothership: int) -> t.Callable[[Contact], bool]:
    """Return the test that a leader goes home to mothership, NO_OBJECT
    for a leader with no way out."""

    def going(leader: Contact) -> bool:
        return leader.activity == Activity.GOING_HOME and leader.target == mothership

    return going


def _in_formation(
    view: CraftView, question: Question, leader: Contact, activity: Activity
) -> Actions:
    """Return a follower's formation flight on its way out, its activity
    the route's and no target."""
    actions = formation.fly(view, question, leader)
    return dataclasses.replace(actions, activity=activity, target=NO_OBJECT)


def _here(view: CraftView) -> tuple[int, int, int]:
    return view.x, view.y, view.z


def _offset(view: CraftView, point: tuple[int, int, int]) -> tuple[int, int, int]:
    return point[0] - view.x, point[1] - view.y, point[2] - view.z


def _toward(view: CraftView, dx: int, dy: int, dz: int) -> Steer:
    yaw, pitch = geometry.bearing(dx, dy, dz)
    return Steer(yaw, pitch, view.roll)

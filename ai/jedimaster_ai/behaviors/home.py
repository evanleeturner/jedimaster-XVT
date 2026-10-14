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
    - else to its home point, where it stops: a craft with no way out

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
    - Pure: the way out is read from the view each think, the activity it
      set last the only memory. The notebook goes back as it came. A new
      way out is logged at DEBUG when the activity changes.
"""

from __future__ import annotations

import logging

from .. import geometry
from .. import tactics
from ..world import Actions
from ..world import Activity
from ..world import Books
from ..world import CraftView
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import Decision

logger = logging.getLogger(__name__)

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
# The starship orders, SS Patrol Loop (21) to SS Patrol and Disable (27), and
# SS Go Home (29): their 1997 plans went home by the starship's rule.
STARSHIP_ORDERS = frozenset((21, 22, 23, 24, 25, 26, 27, 29))


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next step out of the battle."""
    own = tactics.own_contact(view, question)
    if own is not None and own.object_kind == OBJECT_KIND_ENTERING_HYPERSPACE:
        return Decision(view.notebook)
    if view.order_code in STARSHIP_ORDERS:
        if view.books & Books.BY_MOTHERSHIP:
            route = "stop"
            actions = Actions(throttle=0, activity=Activity.NONE, target=NO_OBJECT)
        elif view.books & Books.MAY_JUMP:
            route, actions = "jump", _jump(view)
        else:
            route, actions = "wait", _wait(view)
    elif view.books & Books.MAY_JUMP:
        route, actions = "jump", _jump(view)
    elif view.mothership != NO_OBJECT:
        route, actions = _hangar(view, question)
    else:
        route, actions = "wait", _wait(view)
    if actions.activity is not None and actions.activity != view.activity:
        logger.debug(
            'home.route object=%d route="%s" from=%d books=%d',
            view.object_index,
            route,
            view.activity,
            view.books,
        )
    return Decision(view.notebook, actions)


def throttle_for(view: CraftView, speed: int) -> int:
    """Return the throttle that gives speed, the 1997 set-speed rule: full
    throttle once the craft's full speed is at most speed, else speed over
    the full speed as a fraction of 65,536."""
    if view.full_speed <= speed:
        return FULL_THROTTLE
    return (speed << 16) // view.full_speed


def _jump(view: CraftView) -> Actions:
    """Fly to the home point at full throttle and depart within JUMP_RANGE
    of it; with no home point, depart where it is."""
    point = view.home_point if view.books & Books.HOME_POINT else _here(view)
    dx, dy, dz = _offset(view, point)
    if geometry.distance(dx, dy, dz) < JUMP_RANGE:
        return Actions(
            throttle=FULL_THROTTLE,
            activity=Activity.LEAVING_BY_HYPERSPACE,
            target=NO_OBJECT,
            depart=True,
        )
    return Actions(
        steer=_toward(view, dx, dy, dz),
        throttle=FULL_THROTTLE,
        activity=Activity.LEAVING_BY_HYPERSPACE,
        target=NO_OBJECT,
    )


def _wait(view: CraftView) -> Actions:
    """Fly home with no way out: to the home point, stopping within
    JUMP_RANGE of it; with no home point, stop where it is."""
    point = view.home_point if view.books & Books.HOME_POINT else _here(view)
    dx, dy, dz = _offset(view, point)
    if geometry.distance(dx, dy, dz) < JUMP_RANGE:
        return Actions(throttle=0, activity=Activity.GOING_HOME, target=NO_OBJECT)
    return Actions(
        steer=_toward(view, dx, dy, dz),
        throttle=FULL_THROTTLE,
        activity=Activity.GOING_HOME,
        target=NO_OBJECT,
    )


def _hangar(view: CraftView, question: Question) -> tuple[str, Actions]:
    """Fly into the mothership: to the outside hangar point, then from
    HANGAR_TURN of it to the inside point, and dock there."""
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
            throttle=throttle_for(view, enter),
            activity=Activity.ENTERING_HANGAR,
            think_interval=ENTER_INTERVAL,
            target=target,
        )
    slow = next((speed for within, speed in APPROACH if reach < within), None)
    return "hangar", Actions(
        steer=_toward(view, dx, dy, dz),
        throttle=FULL_THROTTLE if slow is None else throttle_for(view, slow),
        activity=Activity.GOING_HOME,
        target=target,
    )


def _here(view: CraftView) -> tuple[int, int, int]:
    return view.x, view.y, view.z


def _offset(view: CraftView, point: tuple[int, int, int]) -> tuple[int, int, int]:
    return point[0] - view.x, point[1] - view.y, point[2] - view.z


def _toward(view: CraftView, dx: int, dy: int, dz: int) -> Steer:
    yaw, pitch = geometry.bearing(dx, dy, dz)
    return Steer(yaw, pitch, view.roll)

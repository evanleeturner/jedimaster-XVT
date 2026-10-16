"""The mission orders, and which behavior flies each.

Purpose:
    A craft flies the order its flight group gives it in the mission. This
    module names the 40 order codes, as ``enum mission_order_code`` in
    ``engine/src/xvt/flight/mission/mission.h`` does, and maps each to the
    behavior that flies it.

Invariants:
    - Every order code maps to drift until its behavior is written. Written:
      Attack (``behaviors.attack``), Hold Steady and its six copies
      (``behaviors.hold``), Go Home and SS Go Home (``behaviors.home``),
      Circle, Circle and Evade, SS Patrol Loop and SS Patrol and Attack
      (``behaviors.patrol``), Kamikaze (``behaviors.kamikaze``), SS Launch
      (``behaviors.starship``), Self Destruct (``behaviors.selfdestruct``),
      Drop Off (``behaviors.dropoff``), and the eight boarding orders, Give,
      Take, Exchange, Capture, Destroy, Pick Up, SS Board and Board to Repair
      (``behaviors.board``).
    - Disable, Protect, Escort, Wait, Wait 2, SS Await Return, SS Protect,
      SS Protect 2 and SS Patrol and Disable map to their own behavior files
      (``behaviors.disable``, ``protect``, ``escort``, ``waiting``,
      ``starship``), which fly them as drift until they are written.
    - A code with no behavior in the table, one no order has included, is
      flown by drift after one WARN line.
"""

from __future__ import annotations

import enum
import logging
import types
import typing as t

from .behaviors import attack
from .behaviors import Behavior
from .behaviors import board
from .behaviors import disable
from .behaviors import drift
from .behaviors import dropoff
from .behaviors import escort
from .behaviors import hold
from .behaviors import home
from .behaviors import kamikaze
from .behaviors import patrol
from .behaviors import protect
from .behaviors import selfdestruct
from .behaviors import starship
from .behaviors import waiting
from .world import CraftView

logger = logging.getLogger(__name__)


class OrderCode(enum.IntEnum):
    """The order codes a mission gives a flight group."""

    HOLD_STEADY = 0
    GO_HOME = 1
    CIRCLE = 2
    CIRCLE_AND_EVADE = 3
    RENDEZVOUS = 4
    DISABLED = 5
    AWAIT_BOARDING = 6
    ATTACK = 7
    ATTACK_ESCORTS = 8
    PROTECT = 9
    ESCORT = 10
    DISABLE = 11
    BOARD_AND_GIVE_CARGO = 12
    BOARD_AND_TAKE_CARGO = 13
    BOARD_AND_EXCHANGE_CARGO = 14
    BOARD_AND_CAPTURE_CARGO = 15
    BOARD_AND_DESTROY_CARGO = 16
    PICK_UP = 17
    DROP_OFF = 18
    WAIT = 19
    WAIT_2 = 20
    SS_PATROL_LOOP = 21
    SS_AWAIT_RETURN = 22
    SS_LAUNCH = 23
    SS_PROTECT = 24
    SS_PROTECT_2 = 25
    SS_PATROL_AND_ATTACK = 26
    SS_PATROL_AND_DISABLE = 27
    HOLD_STEADY_2 = 28
    SS_GO_HOME = 29
    HOLD_STEADY_3 = 30
    SS_BOARD = 31
    BOARD_TO_REPAIR = 32
    HOLD_STEADY_4 = 33
    HOLD_STEADY_5 = 34
    HOLD_STEADY_6 = 35
    SELF_DESTRUCT = 36
    KAMIKAZE = 37
    HOLD_STEADY_7 = 38
    NULL = 39


# Hold Steady and the six codes whose 1997 plan was Hold Steady's.
HOLD_STEADY_ORDERS = frozenset(
    {
        OrderCode.HOLD_STEADY,
        OrderCode.HOLD_STEADY_2,
        OrderCode.HOLD_STEADY_3,
        OrderCode.HOLD_STEADY_4,
        OrderCode.HOLD_STEADY_5,
        OrderCode.HOLD_STEADY_6,
        OrderCode.HOLD_STEADY_7,
    }
)

BEHAVIORS: t.Mapping[int, Behavior] = types.MappingProxyType(
    {code: drift.think for code in OrderCode}
    | {code: hold.think for code in HOLD_STEADY_ORDERS}
    | {
        OrderCode.GO_HOME: home.think,
        OrderCode.ATTACK: attack.think,
        OrderCode.SS_GO_HOME: home.think,
        OrderCode.CIRCLE: patrol.think,
        OrderCode.CIRCLE_AND_EVADE: patrol.think,
        OrderCode.SS_PATROL_LOOP: patrol.think,
        OrderCode.SS_PATROL_AND_ATTACK: patrol.think,
        OrderCode.PROTECT: protect.think,
        OrderCode.ESCORT: escort.think,
        OrderCode.DISABLE: disable.think,
        OrderCode.WAIT: waiting.think,
        OrderCode.WAIT_2: waiting.think,
        OrderCode.SS_AWAIT_RETURN: starship.await_return,
        OrderCode.SS_LAUNCH: starship.await_launch,
        OrderCode.SS_PROTECT: starship.protect,
        OrderCode.SS_PROTECT_2: starship.protect,
        OrderCode.SS_PATROL_AND_DISABLE: starship.patrol_and_disable,
        OrderCode.SELF_DESTRUCT: selfdestruct.think,
        OrderCode.DROP_OFF: dropoff.think,
        OrderCode.KAMIKAZE: kamikaze.think,
    }
    | {code: board.think for code in board.ORDERS}
)


def behavior_for(view: CraftView, behaviors: t.Mapping[int, Behavior]) -> Behavior:
    """Return the behavior that flies the craft's order."""
    behavior = behaviors.get(view.order_code)
    if behavior is None:
        logger.warning(
            "order.no_behavior object=%d code=%d", view.object_index, view.order_code
        )
        return drift.think
    return behavior

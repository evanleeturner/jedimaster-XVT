"""The mission orders, and which behavior flies each.

Purpose:
    A craft flies the order its flight group gives it in the mission. This
    module names the 40 order codes, as ``enum mission_order_code`` in
    ``engine/src/xvt/flight/mission/mission.h`` does, and maps each to the
    behavior that flies it.

Invariants:
    - Every order code maps to drift until its behavior is written. Written:
      Attack (``behaviors.attack``).
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
from .behaviors import drift
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


BEHAVIORS: t.Mapping[int, Behavior] = types.MappingProxyType(
    {code: drift.think for code in OrderCode} | {OrderCode.ATTACK: attack.think}
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

"""Self Destruct (order 36): the craft stops and destroys itself after a delay.

Purpose:
    The behavior that flies Self Destruct. Its 1997 plan (selfdestroypln)
    flew the stop maneuver, as Hold Steady does (``hold``), with one order
    condition (``paiorder_killselforder``): while no countdown ran, it set
    the craft's lifetime timer to the order's variable1 times UNIT ticks,
    and the engine destroyed the craft when the timer ran out. Here the
    craft asks for that countdown with the ``self_destruct`` action on its
    first think of the order slot, and the engine starts it only while none
    runs. Meanwhile the craft sits at throttle 0, and its turrets defend, as
    every craft's do, though the 1997 plan had no turret condition.

    - A variable1 of 0 asks for 2 to 5 units at random, as 1997 did (ruling
      86), rolled on the craft's dice so every machine of a network game
      picks the same delay.
    - A delay of more than UNITS_MAX units would wrap the engine's 16-bit
      lifetime timer, so it is never sent (ruling 85): the craft sits on,
      after one WARN line.
    - The order has no goal: the craft never moves to a later order, and the
      engine keeps no books for it that would send it home.

The state, SLOT.size bytes at the start of the scratch: u8 the order slot
the craft has answered, in phase ANSWERED: it asked for the countdown, or
found the delay too long.

Invariants:
    - Each order slot is answered once: the countdown asked for, or the WARN
      line written. The engine applies none of an answer it refuses, the
      notebook included, so a refused answer is asked again at the next
      think.
    - Pure: the decision depends only on the view and the question.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import dice
from .. import notebook
from ..world import CraftView
from ..world import Question
from . import BehaviorCode
from . import Decision
from . import hold

logger = logging.getLogger(__name__)

CODE = BehaviorCode.SELF_DESTRUCT
# Ticks per unit of the order's variable1, as 1997 counted the delay: five of
# the flight code's simulated seconds.
UNIT = 1180
# The longest delay the engine's 16-bit lifetime timer holds: 55 units are
# 64,900 ticks, 56 would be 66,080.
UNITS_MAX = 55
# A variable1 of 0: 1997's (rand & 3) + 2, so 2 to 5 units.
RANDOM_LOW = 2
RANDOM_SPREAD = 4
SLOT = struct.Struct("<B")
ANSWERED = 1


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's step: stopped, and on its first think of the order
    slot the countdown its order asks for."""
    book = notebook.open_for(view, CODE, question.tick)
    if book.phase == ANSWERED and SLOT.unpack_from(book.scratch)[0] == view.order_slot:
        return hold.think(view, question)
    units, state = _delay(view, book.dice)
    book = dataclasses.replace(
        book.in_phase(ANSWERED, question.tick),
        dice=state,
        scratch=SLOT.pack(view.order_slot) + book.scratch[SLOT.size :],
    )
    if units > UNITS_MAX:
        logger.warning(
            "selfdestruct.too_long object=%d slot=%d units=%d",
            view.object_index,
            view.order_slot,
            units,
        )
        return Decision(book.encode(), hold.HOLD)
    logger.info(
        "selfdestruct.start object=%d slot=%d ordered=%d units=%d ticks=%d",
        view.object_index,
        view.order_slot,
        view.order_variable1,
        units,
        units * UNIT,
    )
    actions = dataclasses.replace(hold.HOLD, self_destruct=units)
    return Decision(book.encode(), actions)


def _delay(view: CraftView, state: int) -> tuple[int, int]:
    """Return the delay the order asks for, in units of UNIT ticks, and the
    dice after it: the order's variable1, or 2 to 5 at random for 0."""
    if view.order_variable1 != 0:
        return view.order_variable1, state
    roll, state = dice.roll(state, RANDOM_SPREAD)
    return RANDOM_LOW + roll, state

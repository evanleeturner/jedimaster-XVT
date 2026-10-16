"""Disable (order 11): fight the order's targets until none of their subsystems work.

Purpose:
    A craft with the Disable order flies Attack's dogfight (``attack.fight``)
    at the nearest craft its order names whose subsystems still work, the
    1997 rule: a target whose last subsystem fails (``Contact.disabled``)
    leaves the list, and the craft turns to the next. At those targets only
    its ion cannons fire, every other cannon group holds, as in 1997
    (``cannons.cannon_fire``), so a craft with no ion cannon flies at its
    target and never fires a cannon: the mission wants the target whole. Its guided warheads fire as
    Attack's do, but by the Disable limit (``warheads.outlasts_disabling``):
    only while the target's front shield, with a tenth of its hull once more
    than a tenth is damaged, outlasts the warheads flying at it and one more
    from each launcher, so they bring the shield down and leave the hull.

    A player's commands come first, as for Attack (``tactics.commanded``):
    the craft an attack or cover order names is fought, with the ion
    cannons alone, until its subsystems fail; then the order's targets come
    back. An evade command is flown as Attack flies it.

    With no target that works, a craft an enemy is after fights that enemy
    while it may fight back (``attack.pursuer``, ``attack.fights_back``),
    with every cannon but the ion cannons, and its warheads, as Attack
    fights back: a chaser is not the order's target; a disabled craft is
    never after it. Otherwise it cruises as Attack seeks (``seek``).

Invariants:
    - It fires at a target whose subsystems work only with ion cannons:
      groups whose shot type is ``cannons.ION_CANNON``.
    - A disabled craft is never fought: not as a target, not as a chaser.
    - The dogfight's notebook is Attack's, so a craft that changes order
      between Attack and Disable keeps its fight.
    - Pure: the decision depends only on the view and the question.
"""

from __future__ import annotations

import logging

from .. import cannons
from .. import tactics
from ..world import Commands
from ..world import Contact
from ..world import CraftView
from ..world import Question
from . import attack
from . import Decision

logger = logging.getLogger(__name__)


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move: Attack's dogfight at the nearest target
    that works, with ion cannons alone and warheads by the Disable limit;
    else at an enemy after it, as Attack fights back, while it may."""
    if view.commands & Commands.EVADE:
        return attack.think(view, question)
    working = targets(view, question)
    if not working:
        chaser = attack.pursuer(view, question)
        if chaser is not None and attack.fights_back(view, question, chaser):
            return attack.fight(view, question, [chaser])
    return attack.fight(view, question, working, disabling=True, cruising=True)


def targets(view: CraftView, question: Question) -> list[Contact]:
    """Return the craft's targets: the craft its order names whose subsystems
    work, a player's commands applied; a commanded craft whose subsystems
    failed is done with, and the order's targets come back."""
    named = tactics.order_targets(view, question)
    working = [contact for contact in named if not contact.disabled]
    chosen = tactics.commanded(view, question, working)
    if any(contact.disabled for contact in chosen):
        chosen = [c for c in working if c.object_index != view.avoid_target]
    logger.debug(
        "disable.targets object=%d named=%d working=%d chosen=%d ion_groups=%d",
        view.object_index,
        len(named),
        len(working),
        len(chosen),
        sum(
            kind == cannons.ION_CANNON
            for kind in view.cannon_type[: view.cannon_group_count]
        ),
    )
    return chosen

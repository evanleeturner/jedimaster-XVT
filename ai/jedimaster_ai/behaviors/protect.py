"""Protect (order 9): fight the craft that attack the order's targets.

Purpose:
    A craft with the Protect order flies Attack's dogfight (``attack.fight``)
    at the nearest enemy attacking a craft its order names, as 1997's
    respond plan did. A named craft counts once a craft of some team has
    hit it (``Contact.attacked``); its attackers are the enemies whose
    target it is, and the one whose shot last hit it (``Contact.hit_by``).
    It fires every cannon and warhead Attack fires.

    A player's commands come first, as for Attack (``tactics.commanded``):
    the craft an attack or cover order names, and never the one an ignore
    order names. An evade command is flown as Attack flies it.

    With no attacker, a craft an enemy is after fights that enemy while it
    may fight back (``attack.pursuer``, ``attack.fights_back``), and
    otherwise cruises as Attack seeks (``seek``).

Invariants:
    - It fights only craft the engine marks hostile (``tactics.enemies``),
      as every behavior does: 1997 checked hostility only for a craft a
      player flew.
    - A named craft no team has hit has no attackers, whatever aims at it.
    - Lists keep the question's contact order, so every machine sees the same.
    - The dogfight's notebook is Attack's.
    - Pure: the decision depends only on the view and the question.
"""

from __future__ import annotations

import logging

from .. import tactics
from ..world import Commands
from ..world import Contact
from ..world import CraftView
from ..world import Question
from . import attack
from . import Decision

logger = logging.getLogger(__name__)


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move: Attack's dogfight at the nearest enemy
    attacking a craft its order names; else at an enemy after it while it
    may fight back."""
    if view.commands & Commands.EVADE:
        return attack.think(view, question)
    targets = tactics.commanded(view, question, attackers(view, question))
    if not targets:
        chaser = attack.pursuer(view, question)
        if chaser is not None and attack.fights_back(view, question, chaser):
            targets = [chaser]
    return attack.fight(view, question, targets, cruising=True)


def attackers(view: CraftView, question: Question) -> list[Contact]:
    """Return the enemies attacking a craft the order names that some team
    has hit: each that targets it, and the one whose shot hit it last."""
    named = [
        contact
        for position, contact in enumerate(question.contacts)
        if view.order_target_mask >> position & 1
    ]
    hit = [contact for contact in named if contact.attacked]
    aimed_at = {contact.object_index for contact in hit}
    hitters = {contact.hit_by for contact in hit}
    found = [
        enemy
        for enemy in tactics.enemies(view, question)
        if enemy.target in aimed_at or enemy.object_index in hitters
    ]
    logger.debug(
        "protect.attackers object=%d named=%d hit=%d attackers=%d",
        view.object_index,
        len(named),
        len(hit),
        len(found),
    )
    return found

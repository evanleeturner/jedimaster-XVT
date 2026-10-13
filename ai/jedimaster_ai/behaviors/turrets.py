"""Turrets: every craft's turrets defend it, whatever its order.

Purpose:
    After a craft's order behavior decides, its turrets get targets: the
    craft's threats, nearest first, shared out over its turret slots in
    turn. A threat is an enemy within TURRET_RANGE that targets the craft,
    last hit it, or is one its order names. With no threat every turret is
    told to hold (NO_OBJECT). The engine aims and fires each turret at its
    target (``laser_fireturretslot``).

Invariants:
    - A behavior that aimed the turrets itself keeps its own choice.
    - The decision's notebook is never changed.
    - Only the order's targets and craft that target or hit this one count
      as threats: the engine does not send the mission's alliances.
"""

from __future__ import annotations

import dataclasses

from .. import geometry
from .. import tactics
from ..world import Contact
from ..world import CraftView
from ..world import NO_OBJECT
from ..world import Question
from ..world import Turret
from ..world import WEAPON_SLOTS
from . import Decision

# The 1997 pilot's longest firing range at a large target: tier 2's 0xA000
# with the 0x6000 bonus, about 1.6 km.
TURRET_RANGE = 0x10000


def defend(view: CraftView, question: Question, decision: Decision) -> Decision:
    """Return the decision with the craft's turrets aimed at its threats."""
    if not view.turret_slot_mask or decision.actions.turrets is not None:
        return decision
    aimed = threats(view, question)
    slots = [slot for slot in range(WEAPON_SLOTS) if view.turret_slot_mask >> slot & 1]
    turrets = tuple(
        Turret(slot, aimed[turn % len(aimed)].object_index if aimed else NO_OBJECT)
        for turn, slot in enumerate(slots)
    )
    actions = dataclasses.replace(decision.actions, turrets=turrets)
    return dataclasses.replace(decision, actions=actions)


def threats(view: CraftView, question: Question) -> list[Contact]:
    """Return the craft's threats within TURRET_RANGE, nearest first."""
    own = tactics.own_contact(view, question)
    hit_by = NO_OBJECT if own is None else own.last_attacker
    named = {contact.object_index for contact in tactics.order_targets(view, question)}
    found = []
    for contact in question.contacts:
        if not tactics.can_fight(view, contact):
            continue
        if not (
            contact.target == view.object_index
            or contact.object_index == hit_by
            or contact.object_index in named
        ):
            continue
        reach = geometry.distance(*tactics.offset(view, contact))
        if reach <= TURRET_RANGE:
            found.append((reach, contact.object_index, contact))
    found.sort(key=lambda entry: (entry[0], entry[1]))
    return [contact for _, _, contact in found]

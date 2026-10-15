"""Turrets: every craft's turrets defend it, whatever its order.

Purpose:
    After a craft's order behavior decides, its turrets get targets: the
    craft's threats, nearest first, shared out over its turret slots in
    turn. A threat is a craft the engine marks hostile, within TURRET_RANGE,
    that targets the craft, last hit it, or is one its order names. With no threat every turret is
    told to hold (NO_OBJECT). The engine aims and fires each turret at its
    target (``laser_fireturretslot``).

    A craft on SS Protect guards (``guard``): its threats come first, then
    the attackers of the craft its order protects, nearest first, so the
    turrets left over once each threat has one take those attackers. That is
    the 1997 order of the two turret rules on the starship protect plan:
    each turret took the craft's own attacker first, and only a turret left
    idle took an attacker of an order target.

Invariants:
    - A behavior that aimed the turrets itself keeps its own choice.
    - The decision's notebook is never changed.
    - A hostile craft that neither targets nor hit this one, and that its
      order does not name, is not a threat; on guard, it is also aimed at
      when it attacks a protected craft.
    - Each craft is in the list of targets once.
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
    return _aim(view, decision, threats(view, question))


def guard(view: CraftView, question: Question, decision: Decision) -> Decision:
    """Return the decision with the craft's turrets aimed at its threats, then
    at the attackers of the craft its order protects (``attackers``)."""
    if not view.turret_slot_mask or decision.actions.turrets is not None:
        return decision
    own = threats(view, question)
    seen = {contact.object_index for contact in own}
    guarded = [
        contact
        for contact in attackers(view, question)
        if contact.object_index not in seen
    ]
    return _aim(view, decision, own + guarded)


def threats(view: CraftView, question: Question) -> list[Contact]:
    """Return the craft's threats within TURRET_RANGE, nearest first."""
    own = tactics.own_contact(view, question)
    hit_by = NO_OBJECT if own is None else own.last_attacker
    named = {contact.object_index for contact in tactics.order_targets(view, question)}
    return _nearest(
        view,
        [
            contact
            for position, contact in enumerate(question.contacts)
            if tactics.can_fight(view, position, contact)
            and (
                contact.target == view.object_index
                or contact.object_index == hit_by
                or contact.object_index in named
            )
        ],
    )


def attackers(view: CraftView, question: Question) -> list[Contact]:
    """Return the craft's enemies within TURRET_RANGE that attack a craft its
    order protects, nearest first.

    A protected craft is one the order names, friend or not, that a craft of
    some team has hit (``Contact.attacked``), unless a player told the craft
    to leave it alone (``CraftView.avoid_target``). Its attacker targets it,
    or is the craft whose shot last hit it (``Contact.hit_by``). The 1997
    test counted a craft in an attack maneuver on it; its target is the
    stand-in.
    """
    protected = [
        contact
        for position, contact in enumerate(question.contacts)
        if view.order_target_mask >> position & 1
        and contact.attacked
        and contact.object_index != view.avoid_target
    ]
    guarded = {contact.object_index for contact in protected}
    hitters = {contact.hit_by for contact in protected}
    return _nearest(
        view,
        [
            contact
            for position, contact in enumerate(question.contacts)
            if tactics.can_fight(view, position, contact)
            and (contact.target in guarded or contact.object_index in hitters)
        ],
    )


def _nearest(view: CraftView, contacts: list[Contact]) -> list[Contact]:
    """Return the contacts within TURRET_RANGE of the craft, nearest first,
    the nearer of two at one range the one of lower object index."""
    found = []
    for contact in contacts:
        reach = geometry.distance(*tactics.offset(view, contact))
        if reach <= TURRET_RANGE:
            found.append((reach, contact.object_index, contact))
    found.sort(key=lambda entry: (entry[0], entry[1]))
    return [contact for _, _, contact in found]


def _aim(view: CraftView, decision: Decision, aimed: list[Contact]) -> Decision:
    """Return the decision with the craft's turret slots given the aimed
    craft in turn, each turret NO_OBJECT when none is aimed."""
    slots = [slot for slot in range(WEAPON_SLOTS) if view.turret_slot_mask >> slot & 1]
    turrets = tuple(
        Turret(slot, aimed[turn % len(aimed)].object_index if aimed else NO_OBJECT)
        for turn, slot in enumerate(slots)
    )
    actions = dataclasses.replace(decision.actions, turrets=turrets)
    return dataclasses.replace(decision, actions=actions)

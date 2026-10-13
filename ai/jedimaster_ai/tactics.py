"""What a craft can tell about the other craft a question shows.

Purpose:
    The judgments every behavior shares: a craft's skill tier, its own entry
    among the contacts, which contacts it may fight, which its order names,
    and where a contact sits from it.

Invariants:
    - A craft never fights itself, its own team, or a craft that is breaking
      up, exploding or in hyperspace. Teams allied by the mission are not
      known here (the engine does not send the mission's alliances), so only
      the order's targets and craft that target this one count as enemies.
    - Lists keep the question's contact order, so every machine sees the same.
"""

from __future__ import annotations

from .world import Contact
from .world import CraftView
from .world import Question

# A skill of 65,536ths: below a half is tier 0, below three quarters tier 1,
# above that tier 2, as the game's AI levels map onto them.
_TIER_1_SKILL = 0x8000
_TIER_2_SKILL = 0xC000
# The engine's craft object kinds a craft may be fought in (craft.h): active
# and disabled; breaking up, exploding and the hyperspace kinds are not.
OBJECT_KIND_ACTIVE = 0
OBJECT_KIND_DISABLED = 2
_FIGHTABLE_KINDS = (OBJECT_KIND_ACTIVE, OBJECT_KIND_DISABLED)
# The engine's craft genus codes (object_genus.h) of large targets.
GENUS_STARSHIP = 4
GENUS_PLATFORM = 5


def skill_tier(skill: int) -> int:
    """Return a skill's tier: 0, 1 or 2."""
    if skill < _TIER_1_SKILL:
        return 0
    return 1 if skill < _TIER_2_SKILL else 2


def own_contact(view: CraftView, question: Question) -> Contact | None:
    """Return the craft's own contact, or None when the question has none."""
    for contact in question.contacts:
        if contact.object_index == view.object_index:
            return contact
    return None


def can_fight(view: CraftView, contact: Contact) -> bool:
    """Return whether the craft may fight a contact."""
    return (
        contact.object_index != view.object_index
        and contact.team != view.team
        and contact.object_kind in _FIGHTABLE_KINDS
    )


def order_targets(view: CraftView, question: Question) -> list[Contact]:
    """Return the contacts the craft's order names that it may fight."""
    return [
        contact
        for position, contact in enumerate(question.contacts)
        if view.order_target_mask >> position & 1 and can_fight(view, contact)
    ]


def offset(view: CraftView, contact: Contact) -> tuple[int, int, int]:
    """Return where a contact sits from the craft, in world units."""
    return contact.x - view.x, contact.y - view.y, contact.z - view.z


def is_large(contact: Contact) -> bool:
    """Return whether a contact is a starship or a platform."""
    return contact.genus in (GENUS_STARSHIP, GENUS_PLATFORM)

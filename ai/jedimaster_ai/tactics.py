"""What a craft can tell about the other craft a question shows.

Purpose:
    The judgments every behavior shares: a craft's skill tier, its own entry
    among the contacts, which contacts it may fight, which its order names,
    where a contact sits from it, how far from it to keep, which
    targets a player's commands leave it, and the throttle that gives a
    speed.

Invariants:
    - A craft fights only a craft the engine marks hostile by the mission's
      alliances (``CraftView.enemy_mask``), never itself, and never one that
      is breaking up, exploding or in hyperspace. The engine never marks the
      craft's own team or a team allied with it.
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
# The engine's craft genus codes (object_genus.h): starships and platforms
# are large targets; with freighters they are the bulky craft that small
# craft keep clear of (ruling 24). Only a starfighter answers homing shots
# with countermeasures.
GENUS_STARFIGHTER = 0
GENUS_TRANSPORT = 1
GENUS_FREIGHTER = 3
GENUS_STARSHIP = 4
GENUS_PLATFORM = 5
_BULKY_GENERA = (GENUS_FREIGHTER, GENUS_STARSHIP, GENUS_PLATFORM)
# How far past three quarters of its size a craft keeps from another: 4,096
# units, about 100 m at the HUD's 40,700 units a kilometre, the break-off
# range every target had before the engine sent sizes.
CLEARANCE_MARGIN = 0x1000
FULL_THROTTLE = 0xFFFF


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


def can_fight(view: CraftView, position: int, contact: Contact) -> bool:
    """Return whether the craft may fight the contact at a position in the
    question's contacts."""
    return (
        contact.object_index != view.object_index
        and bool(view.enemy_mask >> position & 1)
        and contact.object_kind in _FIGHTABLE_KINDS
    )


def enemies(view: CraftView, question: Question) -> list[Contact]:
    """Return every contact the craft may fight."""
    return [
        contact
        for position, contact in enumerate(question.contacts)
        if can_fight(view, position, contact)
    ]


def order_targets(view: CraftView, question: Question) -> list[Contact]:
    """Return the contacts the craft's order names that it may fight."""
    return [
        contact
        for position, contact in enumerate(question.contacts)
        if view.order_target_mask >> position & 1 and can_fight(view, position, contact)
    ]


def commanded(
    view: CraftView, question: Question, targets: list[Contact]
) -> list[Contact]:
    """Return the targets the craft fights once a player's commands apply:
    the craft an attack or cover order gave it, alone, while it may fight
    it; else targets without the craft an ignore order told it to leave
    alone. Each is matched by object index (the 1997 searches, bugs #50 to
    #52, read the object table at 0xFFFF, compared the ignored craft with a
    loop count, and stopped at the first static object they skipped)."""
    for contact in enemies(view, question):
        if contact.object_index == view.command_target:
            return [contact]
    return [contact for contact in targets if contact.object_index != view.avoid_target]


def offset(view: CraftView, contact: Contact) -> tuple[int, int, int]:
    """Return where a contact sits from the craft, in world units."""
    return contact.x - view.x, contact.y - view.y, contact.z - view.z


def is_large(contact: Contact) -> bool:
    """Return whether a contact is a starship or a platform."""
    return contact.genus in (GENUS_STARSHIP, GENUS_PLATFORM)


def is_bulky(contact: Contact) -> bool:
    """Return whether a contact is a freighter, a starship or a platform."""
    return contact.genus in _BULKY_GENERA


def clearance(contact: Contact) -> int:
    """Return how far from a contact's position a craft keeps: three
    quarters of its size, plus CLEARANCE_MARGIN.

    The size is the longest side of the model's box, and the box need not
    sit centred on the position: a Star Destroyer's longest side is 65,396
    units, and its farthest corner 46,909 from its position. Three quarters
    reaches past the farthest corner of every model measured from a
    freighter up, half does not, and the margin covers the small craft
    (``tests/test_tactics.py``).
    """
    return contact.size * 3 // 4 + CLEARANCE_MARGIN


def throttle_for(view: CraftView, speed: int) -> int:
    """Return the throttle that gives speed, the 1997 set-speed rule: full
    throttle once the craft's full speed is at most speed, else speed over
    the full speed as a fraction of 65,536."""
    if view.full_speed <= speed:
        return FULL_THROTTLE
    return (speed << 16) // view.full_speed

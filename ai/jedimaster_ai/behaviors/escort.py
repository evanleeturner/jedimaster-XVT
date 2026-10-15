"""Escort (order 10): keep station beside the craft escorted, and fight the
craft that come after its flight group.

Purpose:
    The engine names the craft escorted (``CraftView.escorted``) and the
    craft's station beside it (``CraftView.station``), with the STATION book,
    by 1997's rules: the first craft with no leader in the flight group of the
    nearest craft the order names, and one of the 28 stations the order's
    variable1 picks, sixteen times as far out for a large escorted craft.

    Each think, as 1997's escort plan did:

    - the craft a player's attack or cover order names, else the nearest
      enemy within THREAT_RANGE of the craft whose target is a craft of the
      escorted craft's flight group (1997's escort threat search), is fought
      with Attack's dogfight (``attack.fight``), every cannon and warhead
      Attack fires; never the one an ignore order names. An evade command is
      flown as Attack flies it
    - with no such enemy, a craft an enemy is after fights that enemy while
      it may fight back (``attack.pursuer``, ``attack.fights_back``)
    - with neither, it keeps station: farther than STATION_RANGE from the
      escorted craft (STATION_RANGE_BIG for one of size BIG_SIZE or more),
      or with the escorted craft disabled, it flies at that craft, a quarter
      turn off to circle it when disabled, at full throttle beyond NEAR and
      at NEAR_THROTTLE within it; otherwise it flies to its station as a
      follower flies to its place (``formation.fly``), the escorted craft
      as its leader. 1997 also matched the escorted craft's roll and pushed
      the craft to its station each tick; the program steers only, as
      formation flight does

    An enemy on its tail with no fight to take it on, or a weave under way,
    is flown as Attack seeks: it weaves. With no craft escorted (none named,
    or one the question does not show) it cruises as Attack seeks
    (``seek``) until one shows.

Invariants:
    - It fights only craft the engine marks hostile (``tactics.enemies``),
      as every behavior does: 1997 took any craft after the escorted group.
    - Keeping station, it holds fire and has no target.
    - The dogfight's notebook is Attack's: a craft keeping station keeps it
      in Attack's SEEK phase, so its next fight starts a new attack run.
    - Lists keep the question's contact order, so every machine sees the same.
    - Pure: the decision depends only on the view and the question.
"""

from __future__ import annotations

import dataclasses
import logging

from .. import geometry
from .. import notebook
from .. import tactics
from ..world import Actions
from ..world import Books
from ..world import Commands
from ..world import Contact
from ..world import CraftView
from ..world import HOLD_FIRE
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import attack
from . import Decision
from . import formation

logger = logging.getLogger(__name__)

FULL_THROTTLE = 0xFFFF
# How far from the craft an enemy after the escorted group is fought: 1997's
# escort threat search, there by its rough distance, here exact.
THREAT_RANGE = 0x40000
# Past this distance from the escorted craft a craft flies straight at it: a
# small escorted craft, and one of size BIG_SIZE or more (1997's escort
# maneuver, the size its type's longest side).
STATION_RANGE = 0x8000
STATION_RANGE_BIG = 0x20000
BIG_SIZE = 3000
# Flying at the escorted craft, full throttle beyond NEAR, NEAR_THROTTLE within.
NEAR = 0x10000
NEAR_THROTTLE = 0x4000


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move: Attack's dogfight at the nearest enemy
    after the escorted group, or at an enemy after it while it may fight
    back; else its station beside the craft escorted."""
    if view.commands & Commands.EVADE:
        return attack.think(view, question)
    host = escorted(view, question)
    targets = tactics.commanded(view, question, threats(view, question, host))
    if not targets:
        chaser = attack.pursuer(view, question)
        if chaser is not None and attack.fights_back(view, question, chaser):
            targets = [chaser]
    decision = attack.fight(view, question, targets, cruising=host is None)
    if host is None or notebook.decode(decision.notebook).phase != attack.SEEK:
        return decision
    return dataclasses.replace(decision, actions=station(view, question, host))


def escorted(view: CraftView, question: Question) -> Contact | None:
    """Return the craft escorted, as the question shows it; None with no
    station given, or with the craft not shown."""
    if view.escorted == NO_OBJECT or not view.books & Books.STATION:
        return None
    for contact in question.contacts:
        if contact.object_index == view.escorted:
            return contact
    return None


def threats(view: CraftView, question: Question, host: Contact | None) -> list[Contact]:
    """Return the enemies within THREAT_RANGE of the craft whose target is a
    craft of the escorted craft's flight group; none with no craft
    escorted."""
    group = (
        set()
        if host is None
        else {
            contact.object_index
            for contact in question.contacts
            if contact.flight_group == host.flight_group
        }
    )
    found = [
        enemy
        for enemy in tactics.enemies(view, question)
        if enemy.target in group
        and geometry.distance(*tactics.offset(view, enemy)) < THREAT_RANGE
    ]
    logger.debug(
        "escort.threats object=%d escorted=%d group=%d threats=%d",
        view.object_index,
        view.escorted if host is None else host.object_index,
        len(group),
        len(found),
    )
    return found


def station(view: CraftView, question: Question, host: Contact) -> Actions:
    """Return the craft's flight to its station beside host, or at host when
    far from it or host is disabled; fire held, no target."""
    to_host = tactics.offset(view, host)
    reach = geometry.distance(*to_host)
    keep = STATION_RANGE_BIG if host.size >= BIG_SIZE else STATION_RANGE
    if reach > keep or host.disabled:
        own = tactics.own_contact(view, question)
        bulky = own is not None and tactics.is_bulky(own)
        yaw, pitch = geometry.bearing(*to_host)
        if host.disabled:
            yaw = (yaw + geometry.QUARTER_TURN) % geometry.TURN
        actions = Actions(
            steer=Steer(yaw, pitch, view.roll),
            throttle=FULL_THROTTLE if reach > NEAR else NEAR_THROTTLE,
            think_interval=formation.INTERVAL_BULKY if bulky else formation.INTERVAL,
        )
    else:
        actions = formation.fly(
            dataclasses.replace(view, place=view.station), question, host
        )
    logger.debug(
        "escort.station object=%d escorted=%d distance=%d range=%d disabled=%d",
        view.object_index,
        host.object_index,
        reach,
        keep,
        int(host.disabled),
    )
    return dataclasses.replace(actions, target=NO_OBJECT, lasers=(HOLD_FIRE, HOLD_FIRE))

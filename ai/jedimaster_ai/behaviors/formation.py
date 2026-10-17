"""Formation: a follower flies to its place beside its leader.

Purpose:
    A craft that follows a leader cruises in formation instead of on its own
    course: the engine gives it its place (``CraftView.place``, the 1997
    formation arithmetic) and the craft flies there with its own turns and
    throttle, as a pilot would. ``patrol`` and ``home`` call it
    where a craft would cruise, and only while its leader flies the same way
    (``leader_for``); a fight, a weave, the turn into a hangar and the jump
    stay theirs. The rule, each think:

    - its place farther than FAR from it (FAR_STARSHIP for a starship) and
      not behind it along the leader's heading: straight at the place at
      full throttle. 1997 measured the far case from the leader and flew at
      it; a mission's formation can set a place farther than FAR behind the
      leader, and a follower there chased the leader past its place for a
      whole flight of the raw10 mission
    - otherwise it steers at a point LOOK ahead of its place along the
      leader's heading, so it closes on the place's line as it flies; a
      place behind it is never turned back to: it keeps aiming ahead and
      slows. Its speed is the leader's plus one for each CLOSE units its
      place lies ahead, less when behind, from half the leader's (SLOWEST)
      to full throttle

    A leader with followers in the battle cruises at most EASED_THROTTLE
    (seven eighths), so a follower with the same top speed has speed to spare
    to close up; fighting or evading, it flies as its behavior says.

    ``jumps_now`` spaces a flight's jumps out: a follower within
    reach of its jump departs at a think with chance 1 in JUMP_ODDS, so the
    flight leaves one by one at moments of its own, as in the films.

Invariants:
    - Pure: the decision depends only on the view and the question. The
      jump roll comes from the craft's own random seed and the tick, so
      every machine of a network game rolls alike, again after a rewind.
    - A craft follows only a leader the engine names, that the question
      shows and that no player flies (formation for a player's wingmen is
      not built), and only with its place given (``Books.PLACE``).
    - It never steers more than a quarter turn off its leader's heading:
      the point it aims at is never behind it along that heading.
"""

from __future__ import annotations

import logging
import typing as t

from .. import dice
from .. import geometry
from .. import tactics
from ..world import Actions
from ..world import Activity
from ..world import Books
from ..world import Contact
from ..world import CraftView
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer

logger = logging.getLogger(__name__)

FULL_THROTTLE = 0xFFFF
# The far case: a follower whose place lies farther than this from it, not
# behind it, flies straight at the place, a starship farther than
# FAR_STARSHIP (1997's reaches, paiman_followleadermaneuver, measured there
# from the leader).
FAR = 0x10000
FAR_STARSHIP = 327_680
# How far ahead of its place along the leader's heading a follower aims: a
# small craft, and a freighter, starship or platform, which turns slower.
LOOK = 0x2000
LOOK_BULKY = 0x8000
# A follower aims at least this share of LOOK ahead, its place behind it.
LOOK_LEAST = 4
# A follower's speed is the leader's plus one for each CLOSE units its place
# lies ahead of it along the leader's heading.
CLOSE = 32
# A follower never asks less than the leader's speed times SLOWEST over 4:
# on the inside of a turn its place swings behind it, and a follower that
# stopped there fell a leg behind.
SLOWEST = 2
# The most a leader with followers cruises at: seven eighths of full.
EASED_THROTTLE = 0xE000
# Ticks between a follower's thinks in formation: a small craft, a bulky one.
INTERVAL = 29
INTERVAL_BULKY = 59
# A follower within reach of its jump departs at a think with chance 1 in
# JUMP_ODDS.
JUMP_ODDS = 8
# The activities of a craft on its way out of the battle.
LEAVING = frozenset(
    {
        Activity.GOING_HOME,
        Activity.GOING_HOME_EVADING,
        Activity.LEAVING_BY_HYPERSPACE,
        Activity.ENTERING_HANGAR,
    }
)


def leader_for(
    view: CraftView, question: Question, flies: t.Callable[[Contact], bool]
) -> Contact | None:
    """Return the leader a craft flies in formation on: the leader the
    engine names, shown in the question, flown by no player, on the craft's
    own team (a captured craft's old leader is not, nor a captured leader
    its old wingmen's), for which ``flies`` holds (it flies the way the
    craft does); None for none, or with no place given."""
    if view.leader == NO_OBJECT or not view.books & Books.PLACE:
        return None
    for contact in question.contacts:
        if contact.object_index == view.leader:
            if contact.player_flown or contact.team != view.team or not flies(contact):
                return None
            return contact
    return None


def on_order(leader: Contact) -> bool:
    """Whether a leader still flies its order: it is not on its way out."""
    return leader.activity not in LEAVING


def fly(view: CraftView, question: Question, leader: Contact) -> Actions:
    """Return a follower's steering, throttle and think interval to its
    place beside its leader, or straight at a place beyond FAR not behind
    it."""
    own = tactics.own_contact(view, question)
    bulky = own is not None and tactics.is_bulky(own)
    far = (
        FAR_STARSHIP if own is not None and own.genus == tactics.GENUS_STARSHIP else FAR
    )
    interval = INTERVAL_BULKY if bulky else INTERVAL
    forward = _forward(leader.yaw, leader.pitch)
    to_place = (view.place[0] - view.x, view.place[1] - view.y, view.place[2] - view.z)
    ahead = _dot(to_place, forward)
    reach = geometry.distance(*to_place)
    if reach > far and ahead >= 0:
        yaw, pitch = geometry.bearing(*to_place)
        logger.debug(
            "formation.far object=%d leader=%d distance=%d",
            view.object_index,
            leader.object_index,
            reach,
        )
        return Actions(
            steer=Steer(yaw, pitch, view.roll),
            throttle=FULL_THROTTLE,
            think_interval=interval,
        )
    look = LOOK_BULKY if bulky else LOOK
    aim_ahead = max(ahead + look, look // LOOK_LEAST)
    aim = tuple(
        to_place[axis] + (forward[axis] * (aim_ahead - ahead) >> 16)
        for axis in range(3)
    )
    yaw, pitch = geometry.bearing(*aim)
    speed = max(leader.speed * SLOWEST // 4, leader.speed + ahead // CLOSE)
    throttle = tactics.throttle_for(view, speed)
    logger.debug(
        "formation.fly object=%d leader=%d ahead=%d aside=%d speed=%d throttle=%d",
        view.object_index,
        leader.object_index,
        ahead,
        geometry.distance(
            *(to_place[axis] - (forward[axis] * ahead >> 16) for axis in range(3))
        ),
        speed,
        throttle,
    )
    return Actions(
        steer=Steer(yaw, pitch, view.roll), throttle=throttle, think_interval=interval
    )


def eased(view: CraftView, question: Question, throttle: int) -> int:
    """Return the throttle a craft cruises at: at most EASED_THROTTLE when
    it leads a follower in the battle, else throttle."""
    if view.leader != NO_OBJECT or throttle <= EASED_THROTTLE:
        return throttle
    for contact in question.contacts:
        if (
            contact.object_index != view.object_index
            and contact.flight_group == view.flight_group
            and contact.team == view.team
            and not contact.player_flown
        ):
            return EASED_THROTTLE
    return throttle


def jumps_now(view: CraftView, tick: int) -> bool:
    """Return whether a follower within reach of its jump departs at this
    think: a roll of 1 in JUMP_ODDS from its seed and the tick."""
    state = dice.seed(view.random_seed, view.object_index)
    return _mixed((state + tick) & dice.MASK) % JUMP_ODDS == 0


def _mixed(value: int) -> int:
    """Return a 32-bit value's bits mixed by MurmurHash3's finalizer.

    The roll needs one number per craft and tick with no state kept, and
    xorshift steps alone are linear: two craft whose states differ by a
    pattern clear of the low bits roll alike at every tick.
    """
    value ^= value >> 16
    value = value * 0x85EBCA6B & dice.MASK
    value ^= value >> 13
    value = value * 0xC2B2AE35 & dice.MASK
    return value ^ value >> 16


def _forward(yaw: int, pitch: int) -> tuple[int, int, int]:
    """Return the unit vector of a heading, in 65,536ths: yaw 0 along +y and
    0x4000 along +x, pitch 0 straight up and 0x4000 level."""
    level = geometry.sine(pitch)
    return (
        geometry.sine(yaw) * level >> 16,
        geometry.sine(yaw + geometry.QUARTER_TURN) * level >> 16,
        geometry.sine(pitch + geometry.QUARTER_TURN),
    )


def _dot(offset: tuple[int, int, int], unit: tuple[int, int, int]) -> int:
    return sum(offset[axis] * unit[axis] for axis in range(3)) >> 16

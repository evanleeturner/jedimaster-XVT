"""Warheads: when a fighting craft fires a warhead, guided or aimed ahead,
and from which launcher.

Purpose:
    The fire control of a craft's warhead launchers in a dogfight
    (``behaviors.attack``), by rulings 52 and 53 and the 1997 pilot's
    limits. A craft fires one guided warhead at its target once all of
    these hold, tested in this order:

    kind      a launcher's warhead may go at the target (KINDS): a big
              target (``big``), a freighter, starship or platform, and in a
              version-14 mission a transport too, takes every kind but the
              pulses; a small one takes the concussion missiles, the proton
              torpedoes and the pulses, never a space bomb, a heavy rocket
              (slower than a proton torpedo) or type 154. In a version-14
              mission a big target takes the pulses too, while its weapon
              inhibit timer is under PULSE_INHIBIT_LIMIT (``goes_at``)
    kept      a craft whose order names a bulky craft it may fight keeps
              its proton torpedoes, advanced ones too, for it (ruling 53)
    launcher  that launcher is ready: its cooldown run out, a warhead left
    lock      the engine declared the craft's lock (``Lock.DECLARED``) and
              counts it on this target, the craft's own target already
              (ruling 52: the player's rule, at every skill)
    fresh     after a shot at a starfighter or a transport, the lock has
              lapsed and been declared again (1997 started it over)
    limit     the target's front shield and hull left outlast the damage of
              the homing warheads already flying at it, that damage divided
              by 16 at a starship or platform and by 4 at a freighter (1997).
              A craft disabling its target (``disabling``) counts its front
              shield alone, with a tenth of its hull once more than a tenth
              is damaged, and adds to the damage flying one warhead from each
              of its launchers whose kind may go at the target: it fires to
              bring the shield down, never to destroy (1997's Disable plan)
    caps      fewer than 2 homing warheads already fly at a small target,
              16 at a big one, each homing pulse among them counting
              PULSE_EXTRA_COUNT more at a big one in a version-14 mission;
              fewer than 1 shot in this attack run at a small target, 6 at
              a big one (1997)

    Of the launchers that pass kind, kept and launcher (only the Missile
    Boat has two), a small target gets the fastest warhead and a big one
    the heaviest: concussion missiles first at fighters, space bombs first
    at big ships. The shot homes with the lock the craft
    holds; the engine turns the lock into the shot's homing.

    A craft at skill tier PAIRED_TIER or above whose hull damage has
    reached its model's threshold (``Books.HURT``) fires both slots of the
    launcher at once (``pairs``), as 1997's top tier did; the attack run
    counts the pair as one shot, as 1997 counted it.

    The unguided shot (rulings 50 and 51): a small craft at skill level
    Jedi or above (UNGUIDED_SKILL_LEVEL, the Sith ranks too) fires at a
    freighter, starship or platform without its lock declared, and a Sith
    Lord at any craft, a player's too (``lord``, missiles idea E). The lock
    test gives way to the aim, tested last:

    aim       the point where the shot meets the target (``lead``) lies
              within half the target's size of the craft's nose line, and
              the shot reaches it within its life

    A shot with no homing flies the craft's nose line at the craft's speed
    plus its kind's until its life ends (the engine's ``object.c``), so the
    meeting point is found as a player leads a shot, in two passes: the
    time the shot takes to the target, where the target flies in that time,
    the time to that point, and where the target flies in that. A lock
    counted to a game second or more still gives the shot that much homing.
    While only the aim holds the shot, the choice names the point, and
    ``behaviors.attack`` turns the craft's nose to it. A warhead a
    freighter, starship or platform fires leaves straight up or down, so
    such a craft never fires one unguided. The engine counts only homing
    warheads as flying at a target, so the run's cap, with the launcher's
    cooldown, is what bounds the unguided shots.

Invariants:
    - A shot fires from one launcher: both its slots only at skill tier
      PAIRED_TIER or above with the craft hurt, else one.
    - A warhead type KINDS does not name never fires.
    - The version-14 rules (a transport as a big target, the pulses at a
      big one, their count toward the cap) hold only when the question's
      ``mission_version`` is SPECIAL_WARHEADS_VERSION.
    - The decision follows from the view, the question, the target, the
      run's count and whether the craft disables the target alone;
      ``behaviors.attack`` keeps the count and the fresh-lock mark in its
      notebook.
    - A declared lock on the target comes first: an unguided shot is aimed
      only while the craft has none.
"""

from __future__ import annotations

import dataclasses
import enum
import logging
import math

from . import geometry
from . import lord
from . import tactics
from .world import Books
from .world import Contact
from .world import CraftView
from .world import Lock
from .world import Question
from .world import Warhead

logger = logging.getLogger(__name__)

# The engine's shot object types of the warheads a flight group can load
# (``laser.c`` ``g_warhead_type_ids``).
PROTON_TORPEDO = 143
CONCUSSION_MISSILE = 144
ADVANCED_PROTON_TORPEDO = 148
ADVANCED_CONCUSSION_MISSILE = 149
SPACE_BOMB = 150
HEAVY_ROCKET = 151
MAGNETIC_PULSE = 152
ION_PULSE = 153
TYPE_154 = 154

# The homing warheads already flying at a target below which another may
# fire, and the shots one attack run may fire at it: small, big (1997).
COMING_CAP = (2, 16)
RUN_CAP = (1, 6)
# The mission file version whose rules send the pulses at a big target and
# count a transport as one (1997's version-14 rules, Balance of Power's).
SPECIAL_WARHEADS_VERSION = 14
# In a version-14 mission a pulse goes at a big target only while the
# target's weapon inhibit timer is under PULSE_INHIBIT_LIMIT ticks, and each
# homing pulse flying at a big target counts PULSE_EXTRA_COUNT more toward
# COMING_CAP (1997).
PULSE_INHIBIT_LIMIT = 0x49C
PULSE_EXTRA_COUNT = 7
# The lowest skill tier (``tactics.skill_tier``) that fires both slots of a
# launcher once hurt: 1997's top tier, 2, and the Sith tiers above it.
PAIRED_TIER = 2
# The damage of the warheads already flying at a starship or platform, and
# at a freighter, is divided by these before the limit compares it (1997).
LARGE_DIVISOR = 16
FREIGHTER_DIVISOR = 4
# A craft disabling its target counts this share of the target's hull,
# once more than that share is damaged (1997's Disable plan).
DISABLE_HULL_SHARE = 10
# The genera after a shot at which the lock must lapse before another
# fires: 1997 set the lock to 0 after a shot at these and kept it at others.
_FRESH_LOCK_GENERA = (tactics.GENUS_STARFIGHTER, tactics.GENUS_TRANSPORT)
# The lowest skill level (``CraftView.skill_level``) that fires an unguided
# warhead at a big ship: Jedi, 5, and the Sith ranks above it (ruling 50).
UNGUIDED_SKILL_LEVEL = 5
# The engine's motion rule (``object.c``): a speed moves
# ((SPEED_SCALE x speed + 128) >> 8) world units in GAME_SECOND ticks, a
# shot as a craft.
SPEED_SCALE = 4660
GAME_SECOND = 236


@dataclasses.dataclass(frozen=True)
class Kind:
    """A warhead kind: its speed and damage before the firer's speed adds
    to them, whether it may go at a small and at a bulky target, and its
    life in game seconds."""

    name: str
    speed: int
    damage: int
    at_small: bool
    at_bulky: bool
    life: int


# Speeds, damages and lives are the engine's table's (``laser.c`` 35-62;
# no warhead's life has a part second). The pulses go at small craft, as
# 1997's class rule sent them, and at big ones only by the version-14 rule
# (``fits``); type 154 at big craft alone, by the class rule.
KINDS = {
    SPACE_BOMB: Kind("space_bomb", 25, 65_000, False, True, 120),
    HEAVY_ROCKET: Kind("heavy_rocket", 175, 35_000, False, True, 90),
    PROTON_TORPEDO: Kind("proton_torpedo", 250, 10_000, True, True, 40),
    ADVANCED_PROTON_TORPEDO: Kind(
        "advanced_proton_torpedo", 300, 15_000, True, True, 45
    ),
    ION_PULSE: Kind("ion_pulse", 350, 6_000, True, False, 40),
    TYPE_154: Kind("type_154", 400, 9_000, False, True, 35),
    CONCUSSION_MISSILE: Kind("concussion_missile", 500, 3_000, True, True, 25),
    ADVANCED_CONCUSSION_MISSILE: Kind(
        "advanced_concussion_missile", 600, 6_000, True, True, 25
    ),
    MAGNETIC_PULSE: Kind("magnetic_pulse", 600, 3_000, True, False, 25),
}
# The kinds a craft whose order names a bulky craft keeps for it (ruling 53).
KEPT_FOR_BULKY = frozenset({PROTON_TORPEDO, ADVANCED_PROTON_TORPEDO})
# The kinds the version-14 rules also send at a big target.
PULSES = frozenset({MAGNETIC_PULSE, ION_PULSE})


class Held(enum.IntEnum):
    """Why a craft holds its warheads: the first test that failed, NONE when
    it fires. The value is the byte ``behaviors.attack`` keeps."""

    NONE = 0
    KIND = 1
    KEPT = 2
    LAUNCHER = 3
    LOCK = 4
    FRESH = 5
    LIMIT = 6
    CAPS = 7
    AIM = 8


@dataclasses.dataclass(frozen=True)
class Lead:
    """Where an unguided shot fired now meets its target: the point's offset
    from the craft, the ticks the shot takes to it and how far from it the
    craft's nose line passes, all in world units but the ticks."""

    dx: int
    dy: int
    dz: int
    ticks: int
    miss: int


@dataclasses.dataclass(frozen=True)
class Choice:
    """A fire decision: the warhead to fire, None when held, and why; for an
    unguided shot, fired or held by the aim alone, where it meets the
    target."""

    warhead: Warhead | None
    held: Held
    lead: Lead | None = None


def choose(
    view: CraftView,
    question: Question,
    target: Contact,
    shots: int,
    fresh_needed: bool,
    disabling: bool = False,
) -> Choice:
    """Return whether the craft fires a guided warhead at its target, and
    from which launcher.

    ``shots`` is the warheads the craft fired in this attack run;
    ``fresh_needed`` is set while a shot at a starfighter or transport waits
    for the lock to lapse; ``disabling`` is set for a craft disabling its
    target, which takes the Disable limit (``outlasts_disabling``). With no
    lock declared, a craft that may fire unguided (``fires_unguided``) is
    held by the aim (``aimed``) alone once every other test passes.
    """
    version = question.mission_version
    bulky = big(question, target)
    allowed = [
        launcher
        for launcher in range(view.warhead_launcher_count)
        if goes_at(view.warhead_type[launcher], target, bulky, version)
    ]
    if not allowed:
        return Choice(None, Held.KIND)
    if not bulky and keeps_for_bulky(view, question):
        allowed = [
            launcher
            for launcher in allowed
            if view.warhead_type[launcher] not in KEPT_FOR_BULKY
        ]
        if not allowed:
            return Choice(None, Held.KEPT)
    ready = [launcher for launcher in allowed if launcher_ready(view, launcher)]
    if not ready:
        return Choice(None, Held.LAUNCHER)
    guided = locked_on(view, question, target)
    if not guided and not fires_unguided(view, question, target):
        return Choice(None, Held.LOCK)
    if fresh_needed:
        return Choice(None, Held.FRESH)
    lasts = (
        outlasts_disabling(view, target, bulky, version)
        if disabling
        else outlasts(target)
    )
    if not lasts:
        return Choice(None, Held.LIMIT)
    if not under_caps(target, shots, bulky, version):
        return Choice(None, Held.CAPS)
    launcher = first_choice(view, ready, bulky)
    paired = pairs(view)
    if guided:
        return Choice(Warhead(launcher, paired), Held.NONE)
    kind = KINDS[view.warhead_type[launcher]]
    point = lead(view, target, kind)
    on = aimed(point, target, kind)
    logger.debug(
        "warheads.lead object=%d target=%d launcher=%d dx=%d dy=%d dz=%d "
        "ticks=%d life=%d miss=%d half=%d aimed=%d",
        view.object_index,
        target.object_index,
        launcher,
        point.dx,
        point.dy,
        point.dz,
        point.ticks,
        kind.life * GAME_SECOND,
        point.miss,
        target.size // 2,
        int(on),
    )
    if not on:
        return Choice(None, Held.AIM, point)
    return Choice(Warhead(launcher, paired), Held.NONE, point)


def big(question: Question, target: Contact) -> bool:
    """Return whether a target takes the big-target warhead rules: a
    freighter, starship or platform, and in a version-14 mission a
    transport too (1997)."""
    if tactics.is_bulky(target):
        return True
    return (
        target.genus == tactics.GENUS_TRANSPORT
        and question.mission_version == SPECIAL_WARHEADS_VERSION
    )


def fits(warhead_type: int, bulky: bool, version: int) -> bool:
    """Return whether a warhead type may go at a big or a small target by
    its class; in a mission of version SPECIAL_WARHEADS_VERSION the pulses
    may go at a big one too."""
    kind = KINDS.get(warhead_type)
    if kind is None:
        return False
    if not bulky:
        return kind.at_small
    return kind.at_bulky or (
        version == SPECIAL_WARHEADS_VERSION and warhead_type in PULSES
    )


def goes_at(warhead_type: int, target: Contact, bulky: bool, version: int) -> bool:
    """Return whether a launcher's warhead may fire at the target now: it
    fits (``fits``), and a pulse at a big target only while the target's
    weapon inhibit timer is under PULSE_INHIBIT_LIMIT (1997)."""
    if not fits(warhead_type, bulky, version):
        return False
    if bulky and warhead_type in PULSES:
        return target.weapon_inhibit_timer < PULSE_INHIBIT_LIMIT
    return True


def pairs(view: CraftView) -> bool:
    """Return whether the craft fires both slots of a launcher at once: at
    skill tier PAIRED_TIER or above, with its hull damage at its model's
    threshold (``Books.HURT``), as 1997's top tier fired."""
    return tactics.skill_tier(view) >= PAIRED_TIER and bool(view.books & Books.HURT)


def keeps_for_bulky(view: CraftView, question: Question) -> bool:
    """Return whether the craft's order names a bulky craft it may fight
    among the contacts shown (ruling 53)."""
    return any(
        tactics.is_bulky(contact) for contact in tactics.order_targets(view, question)
    )


def launcher_ready(view: CraftView, launcher: int) -> bool:
    """Return whether a launcher may fire: its cooldown run out and a warhead
    left."""
    return view.launcher_cooldown[launcher] == 0 and view.warheads_left[launcher] > 0


def made(view: CraftView, launcher: int) -> bool:
    """Return whether the engine made the warhead a look asked launcher for:
    its cooldown is running. The engine starts the wait only when it made a
    warhead (or the launcher was empty, which a look that asked never sees),
    and the next look comes far inside it."""
    return view.launcher_cooldown[launcher] > 0


def locked_on(view: CraftView, question: Question, target: Contact) -> bool:
    """Return whether the engine declared the craft's lock on the target:
    the lock is declared and the craft's own contact names the target, so
    the count was made on it."""
    if view.lock_state != Lock.DECLARED:
        return False
    own = tactics.own_contact(view, question)
    return own is not None and own.target == target.object_index


def fires_unguided(view: CraftView, question: Question, target: Contact) -> bool:
    """Return whether the craft may fire an unguided warhead at the target:
    its skill level is UNGUIDED_SKILL_LEVEL or more, the target is bulky or
    the craft a Sith Lord (``lord.is_lord``), and the craft's own contact,
    which the question must show, is small."""
    if view.skill_level < UNGUIDED_SKILL_LEVEL:
        return False
    if not (tactics.is_bulky(target) or lord.is_lord(view)):
        return False
    own = tactics.own_contact(view, question)
    return own is not None and not tactics.is_bulky(own)


def pace(speed: int) -> int:
    """Return the world units a speed moves in a game second, by the
    engine's motion rule."""
    return (SPEED_SCALE * speed + 128) >> 8


def lead(view: CraftView, target: Contact, kind: Kind) -> Lead:
    """Return where a warhead of a kind, fired now along the craft's nose
    with no homing, meets the target flying on at its speed and heading:
    two passes of the time to meet and the target's flight in that time."""
    dx, dy, dz = tactics.offset(view, target)
    shot = pace(view.speed + kind.speed)
    drift = pace(target.speed)
    ux, uy, uz = geometry.forward(target.yaw, target.pitch)
    px, py, pz = dx, dy, dz
    for _ in range(2):
        flown = geometry.distance(px, py, pz) * drift // shot
        px, py, pz = (
            dx + (ux * flown >> 16),
            dy + (uy * flown >> 16),
            dz + (uz * flown >> 16),
        )
    square = px * px + py * py + pz * pz
    reach = math.isqrt(square)
    nx, ny, nz = geometry.forward(view.yaw, view.pitch)
    along = (px * nx + py * ny + pz * nz) >> 16
    miss = reach if along <= 0 else math.isqrt(max(square - along * along, 0))
    return Lead(px, py, pz, reach * GAME_SECOND // shot, miss)


def aimed(point: Lead, target: Contact, kind: Kind) -> bool:
    """Return whether an unguided shot meets the target: the craft's nose
    line passes within half the target's size of the meeting point, and the
    shot reaches it before its life ends."""
    return point.miss < target.size // 2 and point.ticks < kind.life * GAME_SECOND


def outlasts(target: Contact) -> bool:
    """Return whether the target's front shield and hull left outlast the
    damage of the homing warheads already flying at it, divided as 1997
    divided it for big ships."""
    left = target.front_shield + target.hull_strength - target.hull_damage
    return left > target.warheads_coming_damage // limit_divisor(target)


def outlasts_disabling(
    view: CraftView, target: Contact, bulky: bool, version: int
) -> bool:
    """Return whether the target's front shield, with a tenth of its hull
    once more than a tenth is damaged, outlasts the damage of the homing
    warheads already flying at it and of one warhead from each of the
    craft's launchers whose kind may go at it by its class (``fits``, the
    weapon inhibit timer aside, as 1997 counted them), divided as
    ``outlasts`` divides."""
    share = target.hull_strength // DISABLE_HULL_SHARE
    durability = target.front_shield + (share if target.hull_damage > share else 0)
    salvo = sum(
        KINDS[kind].damage
        for kind in view.warhead_type[: view.warhead_launcher_count]
        if fits(kind, bulky, version)
    )
    coming = (target.warheads_coming_damage + salvo) // limit_divisor(target)
    lasts = durability > coming
    logger.debug(
        "warheads.disable_limit object=%d target=%d durability=%d salvo=%d "
        "coming=%d lasts=%d",
        view.object_index,
        target.object_index,
        durability,
        salvo,
        coming,
        int(lasts),
    )
    return lasts


def limit_divisor(target: Contact) -> int:
    """Return what the damage already flying at a target is divided by:
    LARGE_DIVISOR at a starship or platform, FREIGHTER_DIVISOR at a
    freighter, else 1."""
    if tactics.is_large(target):
        return LARGE_DIVISOR
    return FREIGHTER_DIVISOR if target.genus == tactics.GENUS_FREIGHTER else 1


def under_caps(target: Contact, shots: int, bulky: bool, version: int) -> bool:
    """Return whether another warhead may fire at the target: fewer homing
    warheads already fly at it than COMING_CAP, each homing pulse counting
    PULSE_EXTRA_COUNT more at a big target in a version-14 mission, and the
    run fired fewer than RUN_CAP."""
    coming = target.warheads_coming
    if bulky and version == SPECIAL_WARHEADS_VERSION:
        coming += PULSE_EXTRA_COUNT * target.pulses_coming
    return coming < COMING_CAP[bulky] and shots < RUN_CAP[bulky]


def first_choice(view: CraftView, launchers: list[int], bulky: bool) -> int:
    """Return the launcher to fire of those ready: the heaviest warhead at a
    big target, the fastest at a small one, the lower launcher on a tie."""

    def rank(launcher: int) -> tuple[int, int]:
        kind = KINDS[view.warhead_type[launcher]]
        return (-(kind.damage if bulky else kind.speed), launcher)

    return min(launchers, key=rank)


def needs_fresh_lock(target: Contact) -> bool:
    """Return whether a shot at the target makes the next wait for the lock
    to lapse: a starfighter or a transport (1997 set its lock to 0)."""
    return target.genus in _FRESH_LOCK_GENERA

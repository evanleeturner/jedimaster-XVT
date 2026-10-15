"""Warheads: when a fighting craft fires a guided warhead, and from which
launcher.

Purpose:
    The fire control of a craft's warhead launchers in a dogfight
    (``behaviors.attack``), by rulings 52 and 53 and the 1997 pilot's
    limits. A craft fires one guided warhead at its target once all of
    these hold, tested in this order:

    kind      a launcher's warhead may go at the target (KINDS): a bulky
              target, a freighter, starship or platform, takes every kind
              but the pulses; a small one takes the concussion missiles, the
              proton torpedoes and the pulses, never a space bomb, a heavy
              rocket (slower than a proton torpedo) or type 154
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
              by 16 at a starship or platform and by 4 at a freighter (1997)
    caps      fewer than 2 homing warheads already fly at a small target,
              16 at a bulky one; fewer than 1 shot in this attack run at a
              small target, 6 at a bulky one (1997)

    Of the launchers that pass kind, kept and launcher (only the Missile
    Boat has two), a small target gets the fastest warhead and a bulky one
    the heaviest: concussion missiles first at fighters, space bombs first
    at big ships. The shot homes with the lock the craft
    holds; the engine turns the lock into the shot's homing.

Invariants:
    - A shot fires from one launcher, one slot at a time: 1997 fired a pair
      only at tier 2 with the craft hurt, and the hull damage that made a
      craft hurt is a model number the view does not carry.
    - A warhead type KINDS does not name never fires.
    - The decision follows from the view, the question, the target and the
      run's count alone; ``behaviors.attack`` keeps the count and the
      fresh-lock mark in its notebook.
"""

from __future__ import annotations

import dataclasses
import enum

from . import tactics
from .world import Contact
from .world import CraftView
from .world import Lock
from .world import Question
from .world import Warhead

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
# fire, and the shots one attack run may fire at it: small, bulky (1997).
COMING_CAP = (2, 16)
RUN_CAP = (1, 6)
# The damage of the warheads already flying at a starship or platform, and
# at a freighter, is divided by these before the limit compares it (1997).
LARGE_DIVISOR = 16
FREIGHTER_DIVISOR = 4
# The genera after a shot at which the lock must lapse before another
# fires: 1997 set the lock to 0 after a shot at these and kept it at others.
_FRESH_LOCK_GENERA = (tactics.GENUS_STARFIGHTER, tactics.GENUS_TRANSPORT)


@dataclasses.dataclass(frozen=True)
class Kind:
    """A warhead kind: its speed and damage before the firer's speed adds
    to them, and whether it may go at a small and at a bulky target."""

    name: str
    speed: int
    damage: int
    at_small: bool
    at_bulky: bool


# Speeds and damages are the engine's table's (``laser.c`` 35-62). The
# pulses go at small craft alone, as 1997's class rule sent them; type 154
# at bulky craft alone, by the same rule.
KINDS = {
    SPACE_BOMB: Kind("space_bomb", 25, 65_000, False, True),
    HEAVY_ROCKET: Kind("heavy_rocket", 175, 35_000, False, True),
    PROTON_TORPEDO: Kind("proton_torpedo", 250, 10_000, True, True),
    ADVANCED_PROTON_TORPEDO: Kind("advanced_proton_torpedo", 300, 15_000, True, True),
    ION_PULSE: Kind("ion_pulse", 350, 6_000, True, False),
    TYPE_154: Kind("type_154", 400, 9_000, False, True),
    CONCUSSION_MISSILE: Kind("concussion_missile", 500, 3_000, True, True),
    ADVANCED_CONCUSSION_MISSILE: Kind(
        "advanced_concussion_missile", 600, 6_000, True, True
    ),
    MAGNETIC_PULSE: Kind("magnetic_pulse", 600, 3_000, True, False),
}
# The kinds a craft whose order names a bulky craft keeps for it (ruling 53).
KEPT_FOR_BULKY = frozenset({PROTON_TORPEDO, ADVANCED_PROTON_TORPEDO})


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


@dataclasses.dataclass(frozen=True)
class Choice:
    """A fire decision: the warhead to fire, None when held, and why."""

    warhead: Warhead | None
    held: Held


def choose(
    view: CraftView,
    question: Question,
    target: Contact,
    shots: int,
    fresh_needed: bool,
) -> Choice:
    """Return whether the craft fires a guided warhead at its target, and
    from which launcher.

    ``shots`` is the warheads the craft fired in this attack run;
    ``fresh_needed`` is set while a shot at a starfighter or transport waits
    for the lock to lapse.
    """
    bulky = tactics.is_bulky(target)
    allowed = [
        launcher
        for launcher in range(view.warhead_launcher_count)
        if fits(view.warhead_type[launcher], bulky)
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
    if not locked_on(view, question, target):
        return Choice(None, Held.LOCK)
    if fresh_needed:
        return Choice(None, Held.FRESH)
    if not outlasts(target):
        return Choice(None, Held.LIMIT)
    if not under_caps(target, shots):
        return Choice(None, Held.CAPS)
    return Choice(Warhead(first_choice(view, ready, bulky)), Held.NONE)


def fits(warhead_type: int, bulky: bool) -> bool:
    """Return whether a warhead type may go at a bulky or a small target."""
    kind = KINDS.get(warhead_type)
    if kind is None:
        return False
    return kind.at_bulky if bulky else kind.at_small


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


def locked_on(view: CraftView, question: Question, target: Contact) -> bool:
    """Return whether the engine declared the craft's lock on the target:
    the lock is declared and the craft's own contact names the target, so
    the count was made on it."""
    if view.lock_state != Lock.DECLARED:
        return False
    own = tactics.own_contact(view, question)
    return own is not None and own.target == target.object_index


def outlasts(target: Contact) -> bool:
    """Return whether the target's front shield and hull left outlast the
    damage of the homing warheads already flying at it, divided as 1997
    divided it for big ships."""
    left = target.front_shield + target.hull_strength - target.hull_damage
    return left > target.warheads_coming_damage // limit_divisor(target)


def limit_divisor(target: Contact) -> int:
    """Return what the damage already flying at a target is divided by:
    LARGE_DIVISOR at a starship or platform, FREIGHTER_DIVISOR at a
    freighter, else 1."""
    if tactics.is_large(target):
        return LARGE_DIVISOR
    return FREIGHTER_DIVISOR if target.genus == tactics.GENUS_FREIGHTER else 1


def under_caps(target: Contact, shots: int) -> bool:
    """Return whether another warhead may fire at the target: fewer homing
    warheads already fly at it than COMING_CAP, and the run fired fewer than
    RUN_CAP."""
    bulky = int(tactics.is_bulky(target))
    return target.warheads_coming < COMING_CAP[bulky] and shots < RUN_CAP[bulky]


def first_choice(view: CraftView, launchers: list[int], bulky: bool) -> int:
    """Return the launcher to fire of those ready: the heaviest warhead at a
    bulky target, the fastest at a small one, the lower launcher on a tie."""

    def rank(launcher: int) -> tuple[int, int]:
        kind = KINDS[view.warhead_type[launcher]]
        return (-(kind.damage if bulky else kind.speed), launcher)

    return min(launchers, key=rank)


def needs_fresh_lock(target: Contact) -> bool:
    """Return whether a shot at the target makes the next wait for the lock
    to lapse: a starfighter or a transport (1997 set its lock to 0)."""
    return target.genus in _FRESH_LOCK_GENERA

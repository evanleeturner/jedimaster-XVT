"""Fence for warheads: each test of a guided shot at its boundary, the order
the tests run in, the launcher a Missile Boat picks, and the unguided shot's
aim, flown in a toy of the engine's motion; a Sith Lord's unguided shot at
any craft (idea E).

The craft (object 4) holds a declared lock on its target (contact 1, object
9), a starfighter with no shield and 1,000 hull left, and one launcher of
concussion missiles, ready; each test changes what it names. Expected
values are the fire rules in ``warheads``, as written before its code ran,
and the engine's warhead table (``laser.c``); the unguided shot's meeting
points were worked by hand, two passes as ``warheads.lead`` makes them, and
matched the ``warheads.lead`` lines the code logged.
"""

from __future__ import annotations

import dataclasses

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import warheads
from jedimaster_ai.warheads import Held
from jedimaster_ai.world import Books
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import Lock
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question
from jedimaster_ai.world import Warhead

ME = 4
FOE = 9
SHIP = 12
ALL_BUT_OWN = 0xFFFFFFFE
STARFIGHTER, TRANSPORT, UTILITY, FREIGHTER, STARSHIP, PLATFORM = range(6)


@pytest.fixture
def craft(view):
    def make(**changes):
        mine = {
            "object_index": ME,
            "skill": 0xFFFF,
            "order_target_mask": 0b10,
            "enemy_mask": ALL_BUT_OWN,
            "warhead_launcher_count": 1,
            "warhead_type": (warheads.CONCUSSION_MISSILE, 0),
            "warheads_left": (4, 0),
            "launcher_cooldown": (0, 0),
            "lock_ticks": 708,
            "lock_needed": 708,
            "lock_state": Lock.DECLARED,
        }
        return view(**(mine | changes))

    return make


@pytest.fixture
def foe(contact):
    def make(**changes):
        theirs = {
            "object_index": FOE,
            "genus": STARFIGHTER,
            "target": NO_OBJECT,
            "front_shield": 0,
            "hull_strength": 1000,
            "hull_damage": 0,
            "warheads_coming": 0,
            "warheads_coming_damage": 0,
        }
        return contact(**(theirs | changes))

    return make


@pytest.fixture
def decide(contact):
    """Return the choice at a target, the craft's own contact first naming
    ``aimed_at`` as its target, then the target and any others."""

    def choose(
        me,
        target,
        *others,
        aimed_at=FOE,
        shots=0,
        fresh=False,
        own=True,
        version=0,
        disabling=False,
    ):
        mine = contact(object_index=ME, team=1, target=aimed_at)
        contacts = ((mine,) if own else ()) + (target, *others)
        question = Question(1000, (me,), contacts, mission_version=version)
        return warheads.choose(me, question, target, shots, fresh, disabling)

    return choose


def test_a_declared_lock_on_its_target_fires_the_ready_launcher(craft, foe, decide):
    choice = decide(craft(), foe())
    assert choice == warheads.Choice(Warhead(0), Held.NONE)


# The design's warhead table: (type, may go at a small craft, at a bulky one).
KIND_TABLE = [
    (150, False, True),
    (151, False, True),
    (143, True, True),
    (148, True, True),
    (153, True, False),
    (154, False, True),
    (144, True, True),
    (149, True, True),
    (152, True, False),
    (60, False, False),
]


@pytest.mark.parametrize(("kind", "small", "bulky"), KIND_TABLE)
def test_each_kind_goes_only_at_the_targets_it_suits(
    craft, foe, decide, kind, small, bulky
):
    me = craft(warhead_type=(kind, 0))
    at_fighter = decide(me, foe(genus=STARFIGHTER))
    at_ship = decide(me, foe(genus=STARSHIP, hull_strength=50_000))
    assert at_fighter.held == (Held.NONE if small else Held.KIND)
    assert at_ship.held == (Held.NONE if bulky else Held.KIND)


@pytest.mark.parametrize("genus", [STARFIGHTER, TRANSPORT, UTILITY])
def test_a_bomb_never_goes_at_a_small_craft_of_any_genus(craft, foe, decide, genus):
    choice = decide(craft(warhead_type=(warheads.SPACE_BOMB, 0)), foe(genus=genus))
    assert choice.held == Held.KIND


@pytest.mark.parametrize("genus", [FREIGHTER, STARSHIP, PLATFORM])
def test_a_bomb_goes_at_every_bulky_genus(craft, foe, decide, genus):
    me = craft(warhead_type=(warheads.SPACE_BOMB, 0))
    assert decide(me, foe(genus=genus, hull_strength=90_000)).held == Held.NONE


@pytest.mark.parametrize(
    ("kind", "held"),
    [
        (warheads.PROTON_TORPEDO, Held.KEPT),
        (warheads.ADVANCED_PROTON_TORPEDO, Held.KEPT),
        (warheads.CONCUSSION_MISSILE, Held.NONE),
    ],
)
def test_protons_are_kept_for_the_big_ship_the_order_names(
    craft, foe, contact, decide, kind, held
):
    me = craft(warhead_type=(kind, 0), order_target_mask=0b110)
    ship = contact(object_index=SHIP, genus=FREIGHTER, team=2)
    assert decide(me, foe(), ship).held == held


def test_protons_go_at_the_named_big_ship_itself(craft, foe, decide):
    me = craft(warhead_type=(warheads.PROTON_TORPEDO, 0))
    ship = foe(genus=FREIGHTER, hull_strength=20_000)
    assert decide(me, ship).held == Held.NONE


def test_protons_go_at_a_fighter_when_no_big_ship_is_named(craft, foe, contact, decide):
    me = craft(warhead_type=(warheads.PROTON_TORPEDO, 0), order_target_mask=0b110)
    other = contact(object_index=SHIP, genus=STARFIGHTER, team=2)
    assert decide(me, foe(), other).held == Held.NONE


def test_a_big_ship_shown_but_not_named_keeps_nothing(craft, foe, contact, decide):
    me = craft(warhead_type=(warheads.PROTON_TORPEDO, 0), order_target_mask=0b10)
    ship = contact(object_index=SHIP, genus=STARSHIP, team=2)
    assert decide(me, foe(), ship).held == Held.NONE


@pytest.mark.parametrize(
    ("cooldown", "left", "held"),
    [(0, 1, Held.NONE), (1, 4, Held.LAUNCHER), (0, 0, Held.LAUNCHER)],
)
def test_the_launcher_must_be_ready(craft, foe, decide, cooldown, left, held):
    me = craft(launcher_cooldown=(cooldown, 0), warheads_left=(left, 0))
    assert decide(me, foe()).held == held


MISSILE_BOAT = {
    "warhead_launcher_count": 2,
    "warhead_type": (warheads.PROTON_TORPEDO, warheads.ADVANCED_CONCUSSION_MISSILE),
    "warheads_left": (4, 4),
}


def test_a_missile_boat_fires_its_fastest_warhead_at_a_fighter(craft, foe, decide):
    assert decide(craft(**MISSILE_BOAT), foe()).warhead == Warhead(1)


def test_a_missile_boat_fires_its_heaviest_warhead_at_a_big_ship(craft, foe, decide):
    ship = foe(genus=STARSHIP, hull_strength=50_000)
    assert decide(craft(**MISSILE_BOAT), ship).warhead == Warhead(0)


def test_a_missile_boat_fires_its_other_launcher_while_the_first_reloads(
    craft, foe, decide
):
    me = craft(**MISSILE_BOAT, launcher_cooldown=(0, 300))
    assert decide(me, foe()).warhead == Warhead(0)


def test_a_bomb_comes_first_at_a_big_ship(craft, foe, decide):
    me = craft(
        warhead_launcher_count=2,
        warhead_type=(warheads.ADVANCED_CONCUSSION_MISSILE, warheads.SPACE_BOMB),
        warheads_left=(4, 4),
    )
    ship = foe(genus=PLATFORM, hull_strength=90_000)
    assert decide(me, ship).warhead == Warhead(1)


def test_a_tie_goes_to_the_lower_launcher(craft, foe, decide):
    me = craft(
        warhead_launcher_count=2,
        warhead_type=(warheads.CONCUSSION_MISSILE, warheads.CONCUSSION_MISSILE),
        warheads_left=(4, 4),
    )
    assert decide(me, foe()).warhead == Warhead(0)


@pytest.mark.parametrize("state", [Lock.NONE, Lock.COUNTING])
def test_no_shot_before_the_lock_is_declared(craft, foe, decide, state):
    assert decide(craft(lock_state=state), foe()).held == Held.LOCK


def test_a_lock_counted_on_another_craft_does_not_fire(craft, foe, decide):
    assert decide(craft(), foe(), aimed_at=SHIP).held == Held.LOCK


def test_with_no_contact_of_its_own_the_lock_cannot_be_read(craft, foe, decide):
    assert decide(craft(), foe(), own=False).held == Held.LOCK


def test_a_lock_waiting_to_lapse_holds_the_shot(craft, foe, decide):
    assert decide(craft(), foe(), fresh=True).held == Held.FRESH


@pytest.mark.parametrize(
    ("genus", "divisor"),
    [(STARFIGHTER, 1), (TRANSPORT, 1), (FREIGHTER, 4), (STARSHIP, 16), (PLATFORM, 16)],
)
def test_the_limit_divides_the_damage_coming_by_target(
    craft, foe, decide, genus, divisor
):
    # Front shield 300 and 1,000 hull less 100 damage: 1,200 left.
    target = {"genus": genus, "front_shield": 300, "hull_damage": 100}
    me = craft(warhead_type=(warheads.ADVANCED_PROTON_TORPEDO, 0))
    survives = foe(**target, warheads_coming_damage=1200 * divisor - 1)
    dies = foe(**target, warheads_coming_damage=1200 * divisor)
    assert decide(me, survives).held == Held.NONE
    assert decide(me, dies).held == Held.LIMIT


@pytest.mark.parametrize(
    ("genus", "coming", "shots", "held"),
    [
        (STARFIGHTER, 1, 0, Held.NONE),
        (STARFIGHTER, 2, 0, Held.CAPS),
        (STARFIGHTER, 0, 1, Held.CAPS),
        (TRANSPORT, 2, 0, Held.CAPS),
        (STARSHIP, 15, 5, Held.NONE),
        (STARSHIP, 16, 0, Held.CAPS),
        (STARSHIP, 0, 6, Held.CAPS),
        (FREIGHTER, 15, 5, Held.NONE),
        (FREIGHTER, 0, 6, Held.CAPS),
    ],
)
def test_the_caps_on_warheads_coming_and_shots_in_a_run(
    craft, foe, decide, genus, coming, shots, held
):
    target = foe(genus=genus, hull_strength=90_000, warheads_coming=coming)
    me = craft(warhead_type=(warheads.ADVANCED_PROTON_TORPEDO, 0))
    assert decide(me, target, shots=shots).held == held


@pytest.mark.parametrize(
    ("changes", "target", "held"),
    [
        ({"warhead_type": (warheads.SPACE_BOMB, 0), "lock_state": 0}, {}, Held.KIND),
        ({"warheads_left": (0, 0), "lock_state": 0}, {}, Held.LAUNCHER),
        ({"lock_state": 0}, {"warheads_coming_damage": 5000}, Held.LOCK),
        ({}, {"warheads_coming": 9, "warheads_coming_damage": 5000}, Held.LIMIT),
    ],
)
def test_the_first_test_that_fails_is_the_reason(
    craft, foe, decide, changes, target, held
):
    assert decide(craft(**changes), foe(**target)).held == held


def test_fresh_comes_before_the_limit(craft, foe, decide):
    target = foe(warheads_coming_damage=5000)
    assert decide(craft(), target, fresh=True).held == Held.FRESH


HURT = Books.TARGETS_LEFT | Books.HURT


@pytest.mark.parametrize(
    ("skill", "level", "books", "paired"),
    [
        (0xC000, 4, HURT, True),
        (0xFFFF, 4, Books.TARGETS_LEFT, False),
        (0xBFFF, 3, HURT, False),
        (0xFFFF, 6, HURT, True),
        (0xFFFF, 7, HURT, True),
    ],
)
def test_a_hurt_craft_at_tier_2_or_above_fires_a_pair(
    craft, foe, decide, skill, level, books, paired
):
    me = craft(skill=skill, skill_level=level, books=books)
    assert decide(me, foe()).warhead == Warhead(0, paired=paired)


def test_the_hurt_book_makes_the_pair_not_the_hull_shown(craft, foe, contact):
    me = craft(skill=0xFFFF)
    battered = contact(object_index=ME, team=1, target=FOE, hull_damage=999)
    question = Question(1000, (me,), (battered, foe()))
    choice = warheads.choose(me, question, foe(), 0, False)
    assert choice.warhead == Warhead(0, paired=False)


def test_a_pair_is_held_as_a_single_shot_is(craft, foe, decide):
    me = craft(books=HURT, launcher_cooldown=(5, 0))
    assert decide(me, foe()) == warheads.Choice(None, Held.LAUNCHER)


@pytest.mark.parametrize(
    ("genus", "fresh"),
    [
        (STARFIGHTER, True),
        (TRANSPORT, True),
        (UTILITY, False),
        (FREIGHTER, False),
        (STARSHIP, False),
        (PLATFORM, False),
    ],
)
def test_only_a_shot_at_a_starfighter_or_transport_needs_a_fresh_lock(
    foe, genus, fresh
):
    assert warheads.needs_fresh_lock(foe(genus=genus)) is fresh


# The unguided shot: a Jedi (skill level 5) with no lock, at the origin, its
# nose along +y and level, flying at speed 81 with one launcher of heavy
# rockets, ready: 81 + 175 = 256, which moves 4,660 world units a game second
# by the engine's rule. Its target is a platform at rest dead ahead.
LEVEL = geometry.LEVEL
UNGUIDED = {
    "skill_level": 5,
    "x": 0,
    "y": 0,
    "z": 0,
    "yaw": 0,
    "pitch": LEVEL,
    "speed": 81,
    "warhead_type": (warheads.HEAVY_ROCKET, 0),
    "lock_ticks": 0,
    "lock_state": Lock.NONE,
}


@pytest.fixture
def jedi(craft):
    def make(**changes):
        return craft(**(UNGUIDED | changes))

    return make


@pytest.fixture
def ship(foe):
    def make(**changes):
        theirs = {
            "genus": PLATFORM,
            "hull_strength": 90_000,
            "x": 0,
            "y": 46_600,
            "z": 0,
            "yaw": 0,
            "pitch": LEVEL,
            "speed": 0,
            "size": 10_250,
        }
        return foe(**(theirs | changes))

    return make


def test_a_jedi_fires_unguided_at_a_big_ship_dead_ahead(jedi, ship, decide):
    choice = decide(jedi(), ship())
    lead = warheads.Lead(0, 46_600, 0, 2_360, 0)
    assert choice == warheads.Choice(Warhead(0), Held.NONE, lead)


def test_a_hurt_jedi_fires_its_unguided_shot_as_a_pair(jedi, ship, decide):
    assert decide(jedi(books=HURT), ship()).warhead == Warhead(0, paired=True)


@pytest.mark.parametrize(
    ("level", "held"),
    [(0, Held.LOCK), (4, Held.LOCK), (5, Held.NONE), (6, Held.NONE), (7, Held.NONE)],
)
def test_the_unguided_shot_starts_at_skill_level_jedi(jedi, ship, decide, level, held):
    assert decide(jedi(skill_level=level), ship()).held == held


@pytest.mark.parametrize(
    ("genus", "held"),
    [
        (STARFIGHTER, Held.LOCK),
        (TRANSPORT, Held.LOCK),
        (UTILITY, Held.LOCK),
        (FREIGHTER, Held.NONE),
        (STARSHIP, Held.NONE),
        (PLATFORM, Held.NONE),
    ],
)
def test_the_unguided_shot_goes_only_at_a_bulky_target(jedi, ship, decide, genus, held):
    me = jedi(warhead_type=(warheads.PROTON_TORPEDO, 0))
    assert decide(me, ship(genus=genus)).held == held


@pytest.mark.parametrize(
    ("genus", "held"),
    [
        (STARFIGHTER, Held.NONE),
        (TRANSPORT, Held.NONE),
        (UTILITY, Held.NONE),
        (FREIGHTER, Held.LOCK),
        (STARSHIP, Held.LOCK),
        (PLATFORM, Held.LOCK),
    ],
)
def test_a_freighter_starship_or_platform_never_fires_unguided(
    jedi, ship, contact, genus, held
):
    me, target = jedi(), ship()
    mine = contact(object_index=ME, team=1, genus=genus, target=FOE)
    question = Question(1000, (me,), (mine, target))
    assert warheads.choose(me, question, target, 0, False).held == held


def test_with_no_contact_of_its_own_no_unguided_shot(jedi, ship, decide):
    assert decide(jedi(), ship(), own=False).held == Held.LOCK


# Idea E: a Sith Lord (skill level 7) fires unguided at any craft, a player's
# too. Its launcher holds concussion missiles, which go at small craft; the
# fighter sits at rest 4,000 dead ahead, 400 across.
LORD = {"skill_level": 7, "warhead_type": (warheads.CONCUSSION_MISSILE, 0)}


def fighter(ship, **changes):
    theirs = {"genus": STARFIGHTER, "hull_strength": 400, "y": 4_000, "size": 400}
    return ship(**(theirs | changes))


@pytest.mark.parametrize(
    ("level", "held"), [(5, Held.LOCK), (6, Held.LOCK), (7, Held.NONE)]
)
def test_only_a_sith_lord_fires_unguided_at_a_fighter(jedi, ship, decide, level, held):
    me = jedi(**(LORD | {"skill_level": level}))
    assert decide(me, fighter(ship)).held == held


def test_a_sith_lord_fires_unguided_at_a_fighter_dead_ahead(jedi, ship, decide):
    choice = decide(jedi(**LORD), fighter(ship))
    assert choice == warheads.Choice(Warhead(0), Held.NONE, choice.lead)
    assert (choice.lead.dy, choice.lead.miss) == (4_000, 0)


@pytest.mark.parametrize(
    "genus", [STARFIGHTER, TRANSPORT, UTILITY, FREIGHTER, STARSHIP, PLATFORM]
)
def test_a_sith_lord_fires_unguided_at_every_genus(jedi, ship, decide, genus):
    me = jedi(skill_level=7, warhead_type=(warheads.PROTON_TORPEDO, 0))
    assert decide(me, ship(genus=genus)).held == Held.NONE


def test_a_sith_lord_fires_unguided_at_a_players_craft(jedi, ship, decide):
    target = fighter(ship, flags=ContactFlags.PLAYER)
    assert decide(jedi(**LORD), target).warhead == Warhead(0)


def test_a_sith_lords_unguided_shot_needs_the_aim(jedi, ship, decide):
    """300 off the nose line misses a craft 400 across: half is 200."""
    choice = decide(jedi(**LORD), fighter(ship, x=300))
    assert (choice.held, choice.lead.miss) == (Held.AIM, 300)


def test_a_sith_lords_lock_on_a_fighter_comes_first(jedi, ship, decide):
    locked = jedi(**LORD, lock_ticks=708, lock_state=Lock.DECLARED)
    assert decide(locked, fighter(ship)) == warheads.Choice(Warhead(0), Held.NONE)


def test_a_sith_lord_fires_one_unguided_shot_a_run_at_a_fighter(jedi, ship, decide):
    assert decide(jedi(**LORD), fighter(ship), shots=1) == warheads.Choice(
        None, Held.CAPS
    )


def test_a_sith_lord_in_a_bulky_craft_fires_no_unguided_shot(jedi, ship, contact):
    me, target = jedi(**LORD), fighter(ship)
    mine = contact(object_index=ME, team=1, genus=FREIGHTER, target=FOE)
    question = Question(1000, (me,), (mine, target))
    assert warheads.choose(me, question, target, 0, False).held == Held.LOCK


# A corvette 40,000 ahead crossing to +x at speed 64, which moves 1,165 world
# units a game second, a quarter of the shot's 4,660: the first pass puts the
# meeting point 10,000 to the side, the second 10,307, a third would 10,326.
def crossing(ship, **changes):
    return ship(
        **{"genus": STARSHIP, "y": 40_000, "yaw": 0x4000, "speed": 64, "size": 6011}
        | changes
    )


@pytest.mark.parametrize(
    ("speed", "pace"), [(0, 0), (1, 18), (3, 55), (25, 455), (64, 1165), (256, 4660)]
)
def test_a_pace_is_the_engines_motion_rule_rounded(speed, pace):
    # (4,660 x speed + 128) >> 8: speed 3 gives 13,980, which rounds up.
    assert warheads.pace(speed) == pace


def test_the_meeting_point_takes_two_passes(jedi, ship, decide):
    choice = decide(jedi(), crossing(ship))
    lead = warheads.Lead(10_307, 40_000, 0, 2_091, 10_307)
    assert choice == warheads.Choice(None, Held.AIM, lead)


# The same corvette on other headings: coming straight at the craft, the
# first pass meets it 30,000 out and the second 32,500; climbing straight up,
# the point rises as the crossing one went aside.
@pytest.mark.parametrize(
    ("yaw", "pitch", "lead", "held"),
    [
        (0x8000, LEVEL, warheads.Lead(0, 32_500, 0, 1_645, 0), Held.NONE),
        (0, 0, warheads.Lead(0, 40_000, 10_307, 2_091, 10_307), Held.AIM),
    ],
)
def test_the_meeting_point_follows_the_targets_heading(
    jedi, ship, decide, yaw, pitch, lead, held
):
    choice = decide(jedi(), crossing(ship, yaw=yaw, pitch=pitch))
    assert (choice.held, choice.lead) == (held, lead)


def test_with_its_nose_on_the_point_it_fires(jedi, ship, decide):
    yaw, pitch = geometry.bearing(10_307, 40_000, 0)
    choice = decide(jedi(yaw=yaw, pitch=pitch), crossing(ship))
    assert choice.warhead == Warhead(0)
    assert choice.lead.miss < 3005


@pytest.mark.parametrize(("x", "held"), [(3004, Held.NONE), (3005, Held.AIM)])
def test_the_nose_line_must_pass_within_half_the_size(jedi, ship, decide, x, held):
    choice = decide(jedi(), ship(x=x, y=40_000, size=6011))
    assert (choice.held, choice.lead.miss) == (held, x)


@pytest.mark.parametrize(
    ("y", "ticks", "held"), [(54_599, 28_319, Held.NONE), (54_600, 28_320, Held.AIM)]
)
def test_the_shot_must_reach_the_point_within_its_life(
    jedi, ship, decide, y, ticks, held
):
    # A space bomb from a craft at rest: speed 25, 455 world units a game
    # second, 120 game seconds of life, 28,320 ticks, 54,600 units.
    me = jedi(speed=0, warhead_type=(warheads.SPACE_BOMB, 0))
    choice = decide(me, ship(y=y))
    assert (choice.held, choice.lead.ticks) == (held, ticks)


def test_a_point_behind_the_nose_is_never_aimed_at(jedi, ship, decide):
    choice = decide(jedi(), ship(y=-20_000))
    assert (choice.held, choice.lead.miss) == (Held.AIM, 20_000)


def test_a_lock_declared_on_the_target_comes_first(jedi, ship, decide):
    locked = jedi(lock_ticks=708, lock_state=Lock.DECLARED)
    assert decide(locked, crossing(ship)) == warheads.Choice(Warhead(0), Held.NONE)


def test_a_lock_counted_on_another_craft_leaves_the_unguided_shot(jedi, ship, decide):
    locked = jedi(lock_ticks=708, lock_state=Lock.DECLARED)
    choice = decide(locked, ship(), aimed_at=SHIP)
    assert (choice.warhead, choice.lead.ticks) == (Warhead(0), 2_360)


@pytest.mark.parametrize(
    ("me", "target", "shots", "fresh", "held"),
    [
        ({"warhead_type": (warheads.MAGNETIC_PULSE, 0)}, {}, 0, False, Held.KIND),
        ({"launcher_cooldown": (5, 0)}, {}, 0, False, Held.LAUNCHER),
        ({}, {}, 0, True, Held.FRESH),
        ({}, {"warheads_coming_damage": 2_000_000}, 0, False, Held.LIMIT),
        ({}, {"warheads_coming": 16}, 0, False, Held.CAPS),
        ({}, {}, 6, False, Held.CAPS),
    ],
)
def test_the_aim_is_tested_last(jedi, ship, decide, me, target, shots, fresh, held):
    choice = decide(jedi(**me), crossing(ship, **target), shots=shots, fresh=fresh)
    assert choice == warheads.Choice(None, held)


def toy_flight(me, target, frame):
    """Return the nearest an unguided shot fired now comes to the target's
    position in its life, the two flown by the engine's motion rule
    (``object.c``): each frame of frame ticks moves a thing frame x its pace
    // 236 world units, cut to whole units, along its heading."""
    kind = warheads.KINDS[me.warhead_type[0]]
    nose = geometry.forward(me.yaw, me.pitch)
    heading = geometry.forward(target.yaw, target.pitch)
    shot_step = frame * warheads.pace(me.speed + kind.speed) // warheads.GAME_SECOND
    target_step = frame * warheads.pace(target.speed) // warheads.GAME_SECOND
    shot = [me.x, me.y, me.z]
    at = [target.x, target.y, target.z]
    nearest = geometry.distance(*(at[i] - shot[i] for i in range(3)))
    for _ in range(kind.life * warheads.GAME_SECOND // frame):
        for axis in range(3):
            shot[axis] += nose[axis] * shot_step >> 16
            at[axis] += heading[axis] * target_step >> 16
        nearest = min(nearest, geometry.distance(*(at[i] - shot[i] for i in range(3))))
    return nearest


@dataclasses.dataclass(frozen=True)
class Flight:
    """A toy flight: the warhead and the firer's speed, then the target's
    genus, size, place, yaw, speed and pitch."""

    warhead: int
    firer_speed: int
    genus: int
    size: int
    place: tuple[int, int, int]
    yaw: int
    speed: int
    pitch: int = LEVEL


# Big ships at speeds and courses a fight meets, by the measured sizes
# (``test_tactics.py``).
FLIGHTS = {
    "corvette crossing 20,000 out": Flight(
        warheads.PROTON_TORPEDO, 225, STARSHIP, 6011, (0, 20_000, 0), 0x4000, 150
    ),
    "corvette crossing 40,000 out": Flight(
        warheads.PROTON_TORPEDO, 225, STARSHIP, 6011, (0, 40_000, 0), 0x4000, 150
    ),
    "frigate climbing across": Flight(
        warheads.ADVANCED_PROTON_TORPEDO,
        200,
        STARSHIP,
        10_368,
        (9000, 25_000, -4000),
        0xA000,
        100,
        pitch=0x3000,
    ),
    "freighter head-on": Flight(
        warheads.CONCUSSION_MISSILE, 225, FREIGHTER, 4960, (3000, 30_000, 0), 0x8000, 80
    ),
    "freighter going away": Flight(
        warheads.HEAVY_ROCKET, 225, FREIGHTER, 4960, (-2000, 15_000, 1000), 0x0800, 80
    ),
    "container crossing, a bomb": Flight(
        warheads.SPACE_BOMB, 120, FREIGHTER, 3100, (0, 12_000, 0), 0xC000, 40
    ),
    "Star Destroyer crossing 60,000 out, a bomb": Flight(
        warheads.SPACE_BOMB, 225, STARSHIP, 65_396, (0, 60_000, 0), 0x4000, 60
    ),
}
# The crossing flights where a shot aimed at the target itself misses.
CROSSING = (
    "corvette crossing 20,000 out",
    "corvette crossing 40,000 out",
    "container crossing, a bomb",
)


def flight(jedi, ship, name):
    flown = FLIGHTS[name]
    me = jedi(speed=flown.firer_speed, warhead_type=(flown.warhead, 0))
    x, y, z = flown.place
    target = ship(
        genus=flown.genus,
        size=flown.size,
        x=x,
        y=y,
        z=z,
        yaw=flown.yaw,
        pitch=flown.pitch,
        speed=flown.speed,
    )
    return me, target


@pytest.mark.parametrize("frame", [1, 4, 8, 16])
@pytest.mark.parametrize("name", sorted(FLIGHTS))
def test_an_unguided_shot_meets_the_ship_it_was_aimed_at(
    jedi, ship, decide, name, frame
):
    me, target = flight(jedi, ship, name)
    lead = decide(me, target).lead
    yaw, pitch = geometry.bearing(lead.dx, lead.dy, lead.dz)
    aimed = dataclasses.replace(me, yaw=yaw, pitch=pitch)
    assert decide(aimed, target).warhead == Warhead(0)
    assert toy_flight(aimed, target, frame) < target.size // 2


@pytest.mark.parametrize("frame", [1, 4, 8, 16])
@pytest.mark.parametrize("name", CROSSING)
def test_a_shot_aimed_at_a_crossing_ship_itself_misses(jedi, ship, decide, name, frame):
    me, target = flight(jedi, ship, name)
    yaw, pitch = geometry.bearing(target.x, target.y, target.z)
    at_it = dataclasses.replace(me, yaw=yaw, pitch=pitch)
    assert decide(at_it, target).held == Held.AIM
    assert toy_flight(at_it, target, frame) >= target.size // 2


# The version-14 rules (1997's ``paifight_fightershootorder``): a transport
# is a big target, a pulse goes at a big one while its weapon inhibit timer
# is under 0x49C, and each homing pulse at a big one counts 7 more.


@pytest.mark.parametrize(
    ("version", "held"),
    [(0, Held.KIND), (12, Held.KIND), (13, Held.KIND), (14, Held.NONE)],
)
def test_a_transport_takes_a_bomb_only_in_a_version_14_mission(
    craft, foe, decide, version, held
):
    me = craft(warhead_type=(warheads.SPACE_BOMB, 0))
    target = foe(genus=TRANSPORT, hull_strength=90_000)
    assert decide(me, target, version=version).held == held


@pytest.mark.parametrize("genus", [STARFIGHTER, UTILITY])
def test_no_other_small_genus_turns_big_in_version_14(craft, foe, decide, genus):
    me = craft(warhead_type=(warheads.SPACE_BOMB, 0))
    target = foe(genus=genus, hull_strength=90_000)
    assert decide(me, target, version=14).held == Held.KIND


@pytest.mark.parametrize(
    ("version", "coming", "shots", "held"),
    [
        (14, 15, 5, Held.NONE),
        (14, 16, 0, Held.CAPS),
        (14, 0, 6, Held.CAPS),
        (12, 2, 0, Held.CAPS),
        (12, 0, 1, Held.CAPS),
    ],
)
def test_a_version_14_transport_takes_the_big_caps(
    craft, foe, decide, version, coming, shots, held
):
    target = foe(genus=TRANSPORT, hull_strength=90_000, warheads_coming=coming)
    me = craft(warhead_type=(warheads.ADVANCED_PROTON_TORPEDO, 0))
    assert decide(me, target, shots=shots, version=version).held == held


@pytest.mark.parametrize(("version", "launcher"), [(12, 1), (14, 0)])
def test_a_missile_boat_fires_its_heaviest_at_a_version_14_transport(
    craft, foe, decide, version, launcher
):
    me = craft(
        warhead_launcher_count=2,
        warhead_type=(
            warheads.ADVANCED_PROTON_TORPEDO,
            warheads.ADVANCED_CONCUSSION_MISSILE,
        ),
        warheads_left=(4, 4),
    )
    target = foe(genus=TRANSPORT, hull_strength=90_000)
    assert decide(me, target, version=version).warhead == Warhead(launcher)


@pytest.mark.parametrize(("damage", "held"), [(1199, Held.NONE), (1200, Held.LIMIT)])
def test_a_version_14_transport_divides_the_damage_coming_by_nothing(
    craft, foe, decide, damage, held
):
    # Front shield 300 and 1,000 hull less 100 damage: 1,200 left.
    me = craft(warhead_type=(warheads.ADVANCED_PROTON_TORPEDO, 0))
    target = foe(
        genus=TRANSPORT,
        front_shield=300,
        hull_damage=100,
        warheads_coming_damage=damage,
    )
    assert decide(me, target, version=14).held == held


@pytest.mark.parametrize("kind", [warheads.MAGNETIC_PULSE, warheads.ION_PULSE])
@pytest.mark.parametrize(
    ("version", "inhibit", "held"),
    [
        (12, 0, Held.KIND),
        (13, 0, Held.KIND),
        (14, 0, Held.NONE),
        (14, 0x49B, Held.NONE),
        (14, 0x49C, Held.KIND),
        (14, 0xFFFF, Held.KIND),
    ],
)
def test_a_pulse_goes_at_a_big_ship_in_version_14_while_its_weapons_are_free(
    craft, foe, decide, kind, version, inhibit, held
):
    me = craft(warhead_type=(kind, 0))
    target = foe(genus=STARSHIP, hull_strength=50_000, weapon_inhibit_timer=inhibit)
    assert decide(me, target, version=version).held == held


@pytest.mark.parametrize(("inhibit", "held"), [(0x49B, Held.NONE), (0x49C, Held.KIND)])
def test_a_pulse_at_a_version_14_transport_reads_its_inhibit_timer(
    craft, foe, decide, inhibit, held
):
    me = craft(warhead_type=(warheads.MAGNETIC_PULSE, 0))
    target = foe(genus=TRANSPORT, weapon_inhibit_timer=inhibit)
    assert decide(me, target, version=14).held == held


def test_a_pulse_at_a_small_craft_never_reads_its_inhibit_timer(craft, foe, decide):
    me = craft(warhead_type=(warheads.MAGNETIC_PULSE, 0))
    target = foe(weapon_inhibit_timer=0xFFFF)
    assert decide(me, target, version=14).held == Held.NONE


@pytest.mark.parametrize(
    ("version", "genus", "coming", "pulses", "held"),
    [
        (14, STARSHIP, 1, 1, Held.NONE),
        (14, STARSHIP, 8, 1, Held.NONE),
        (14, STARSHIP, 9, 1, Held.CAPS),
        (14, STARSHIP, 2, 2, Held.CAPS),
        (12, STARSHIP, 2, 2, Held.NONE),
        (14, STARFIGHTER, 1, 1, Held.NONE),
    ],
)
def test_each_pulse_at_a_big_ship_counts_8_toward_the_cap_in_version_14(
    craft, foe, decide, version, genus, coming, pulses, held
):
    target = foe(
        genus=genus, hull_strength=90_000, warheads_coming=coming, pulses_coming=pulses
    )
    me = craft(warhead_type=(warheads.ADVANCED_PROTON_TORPEDO, 0))
    assert decide(me, target, version=version).held == held


@pytest.mark.parametrize(
    ("version", "shield", "held"),
    [
        (14, 4501, Held.NONE),
        (14, 4500, Held.LIMIT),
        (12, 4500, Held.NONE),
        (12, 3750, Held.LIMIT),
    ],
)
def test_a_disabling_craft_counts_its_pulse_at_a_big_ship_in_version_14(
    craft, foe, decide, version, shield, held
):
    # Torpedo 15,000 and pulse 3,000, divided by 4 at a freighter: 4,500 in
    # version 14, the torpedo's 3,750 alone before it. The pulse counts
    # while the inhibit timer holds it back, as 1997 counted it.
    me = craft(
        warhead_launcher_count=2,
        warhead_type=(warheads.ADVANCED_PROTON_TORPEDO, warheads.MAGNETIC_PULSE),
        warheads_left=(4, 4),
    )
    target = foe(genus=FREIGHTER, front_shield=shield, weapon_inhibit_timer=0xFFFF)
    assert decide(me, target, version=version, disabling=True).held == held


def test_protons_are_not_kept_for_a_version_14_transport(craft, foe, contact, decide):
    me = craft(warhead_type=(warheads.PROTON_TORPEDO, 0), order_target_mask=0b110)
    named = contact(object_index=SHIP, genus=TRANSPORT, team=2)
    assert decide(me, foe(), named, version=14).held == Held.NONE


def test_a_jedi_fires_no_unguided_shot_at_a_version_14_transport(jedi, ship, decide):
    assert decide(jedi(), ship(genus=TRANSPORT), version=14).held == Held.LOCK

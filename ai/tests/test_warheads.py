"""Fence for warheads: each test of a guided shot at its boundary, the order
the tests run in, and the launcher a Missile Boat picks.

The craft (object 4) holds a declared lock on its target (contact 1, object
9), a starfighter with no shield and 1,000 hull left, and one launcher of
concussion missiles, ready; each test changes what it names. Expected
values are the fire rules in ``warheads``, as written before its code ran,
and the engine's warhead table (``laser.c``).
"""

from __future__ import annotations

import pytest

from jedimaster_ai import warheads
from jedimaster_ai.warheads import Held
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

    def choose(me, target, *others, aimed_at=FOE, shots=0, fresh=False, own=True):
        mine = contact(object_index=ME, team=1, target=aimed_at)
        contacts = ((mine,) if own else ()) + (target, *others)
        question = Question(1000, (me,), contacts)
        return warheads.choose(me, question, target, shots, fresh)

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


def test_a_shot_never_fires_a_pair(craft, foe, contact, decide):
    # Tier 2 and badly hurt: 1997's one case for a pair.
    me = craft(skill=0xFFFF)
    hurt = contact(object_index=ME, team=1, target=FOE, hull_damage=999)
    question = Question(1000, (me,), (hurt, foe()))
    choice = warheads.choose(me, question, foe(), 0, False)
    assert choice.warhead == Warhead(0, paired=False)


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

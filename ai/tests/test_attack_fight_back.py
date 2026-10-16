"""Fence for attack: a craft with no order target turns on the enemy after it.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; contact 0 is its own, contact 1 its order's target
(``attack_setup``).
"""

from __future__ import annotations

import pytest
from attack_setup import ME
from attack_setup import phase_of

from jedimaster_ai import notebook
from jedimaster_ai.behaviors import attack
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import Question

# Rulings 34, 42 and 43: with no order target in the battle, the enemy after
# the craft. HURT leaves 299 of 400, under three quarters.
HURT = {"hull_strength": 400, "hull_damage": 101}
BEHIND = {"object_index": 12, "y": -0x2000, "target": ME}
AHEAD = BEHIND | {"y": 0x2000}


def test_with_no_order_target_it_turns_on_the_enemy_that_hit_it(
    craft, foe, ask
) -> None:
    decision = ask(craft(order_target_mask=0), foe(**AHEAD), last_attacker=12)
    assert phase_of(decision) == attack.ATTACK
    assert decision.actions.target == 12
    assert decision.actions.lasers != (HOLD_FIRE, HOLD_FIRE)


def test_with_no_order_target_it_weaves_from_a_chaser_then_fights_it(
    craft, foe, ask
) -> None:
    first = ask(craft(order_target_mask=0), foe(**BEHIND), tick=1000)
    assert phase_of(first) == attack.EVADE and first.actions.target == 12
    me = craft(order_target_mask=0, notebook=first.notebook)
    overshot = ask(me, foe(**AHEAD), tick=1016)
    assert phase_of(overshot) == attack.ATTACK and overshot.actions.target == 12


def test_a_damaged_craft_with_no_order_target_only_weaves(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    decision = ask(craft(order_target_mask=0), foe(**BEHIND), **HURT)
    assert phase_of(decision) == attack.EVADE
    assert decision.actions.target == NO_OBJECT
    assert decision.actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert (
        f"attack.pursued object={ME} pursuer=12 hull_left=299 hull_strength=400 "
        "fights=0" in caplog.text
    )


def test_a_weaves_rest_outlasts_the_search(craft, foe, ask) -> None:
    """A damaged craft with no order target that cannot shake its chaser rests
    after a full weave, as one with a target does."""
    decision, phases = None, []
    for tick in range(1000, 1000 + 400 * 16, 16):
        notes = {} if decision is None else {"notebook": decision.notebook}
        decision = ask(
            craft(order_target_mask=0, **notes), foe(**BEHIND), tick=tick, **HURT
        )
        phases.append((tick, phase_of(decision)))
    first_end = next(at for at, phase in phases if phase == attack.SEEK)
    assert 1000 + 1416 <= first_end < 1000 + 1886 + 16
    again = next(at for at, phase in phases if phase == attack.EVADE and at > first_end)
    assert first_end + 1886 <= again < first_end + 1886 + 16


def test_an_enemy_after_the_craft_hit_it_or_sits_on_its_tail(craft, foe, shown) -> None:
    me = craft(order_target_mask=0)
    assert attack.pursuer(me, shown(me, foe(**BEHIND))).object_index == 12
    assert attack.pursuer(me, shown(me, foe(**AHEAD))) is None
    hit = shown(me, foe(**AHEAD), last_attacker=12)
    assert attack.pursuer(me, hit).object_index == 12
    elsewhere = foe(**BEHIND | {"target": 15})
    assert attack.pursuer(me, shown(me, elsewhere, last_attacker=12)) is None
    friendly = craft(order_target_mask=0, enemy_mask=0)
    assert attack.pursuer(friendly, shown(friendly, foe(**BEHIND))) is None
    nearer = foe(**BEHIND | {"object_index": 15, "y": -0x1000})
    assert attack.pursuer(me, shown(me, foe(**BEHIND), nearer)).object_index == 15


@pytest.mark.parametrize("genus", [3, 4, 5])
def test_a_freighter_starship_or_platform_is_never_after_the_craft(
    craft, foe, shown, genus
) -> None:
    me = craft(order_target_mask=0)
    big = foe(**BEHIND | {"genus": genus})
    assert attack.pursuer(me, shown(me, big, last_attacker=12)) is None


def test_the_enemy_it_fights_stays_after_it_while_it_targets_the_craft(
    craft, foe, ask, shown
) -> None:
    fought = ask(craft(order_target_mask=0), foe(**AHEAD), last_attacker=12)
    me = craft(order_target_mask=0, notebook=fought.notebook)
    nearer = foe(**BEHIND | {"object_index": 15, "y": -0x1000})
    assert attack.pursuer(me, shown(me, foe(**AHEAD), nearer)).object_index == 12
    turned = foe(**AHEAD | {"target": 15})
    assert attack.pursuer(me, shown(me, turned)) is None


DISABLED = {"flags": ContactFlags.DISABLED}


def test_a_disabled_enemy_is_never_after_the_craft(craft, foe, ask, shown) -> None:
    """With none of its subsystems working it can neither fly nor fire: no
    pursuer as the last hitter or as the enemy fought, and on no tail."""
    me = craft(order_target_mask=0)
    dead = foe(**BEHIND | DISABLED)
    assert attack.pursuer(me, shown(me, dead, last_attacker=12)) is None
    assert attack.tail_threat(me, shown(me, dead)) is None
    fought = ask(me, foe(**AHEAD), last_attacker=12)
    me = craft(order_target_mask=0, notebook=fought.notebook)
    nearer = foe(**BEHIND | {"object_index": 15, "y": -0x1000})
    question = shown(me, foe(**AHEAD | DISABLED), nearer)
    assert attack.pursuer(me, question).object_index == 15


def test_with_only_a_disabled_enemy_behind_and_nowhere_to_cruise_it_holds_its_course(
    craft, foe, ask
) -> None:
    """Neither fought back nor woven from."""
    dead = foe(**BEHIND | DISABLED)
    decision = ask(craft(order_target_mask=0), dead, last_attacker=12)
    assert decision.actions.target == NO_OBJECT
    assert decision.actions.steer is None
    assert phase_of(decision) == attack.SEEK


@pytest.mark.parametrize(
    ("strength", "damage", "fights"),
    [(400, 100, True), (400, 101, False), (400, 400, False), (0, 0, True)],
)
def test_it_fights_back_at_three_quarters_of_its_hull_or_more(
    craft, foe, shown, strength, damage, fights
) -> None:
    """Shields never count: both are down in every case."""
    me, chaser = craft(), foe(**BEHIND)
    question = shown(
        me,
        chaser,
        hull_strength=strength,
        hull_damage=damage,
        front_shield=0,
        rear_shield=0,
    )
    assert attack.fights_back(me, question, chaser) is fights


@pytest.mark.parametrize("changes", [{"cannon_group_count": 0}, {"full_speed": 0}])
def test_a_craft_without_cannons_or_engines_never_fights_back(
    craft, foe, shown, changes
) -> None:
    me, chaser = craft(**changes), foe(**BEHIND)
    assert not attack.fights_back(me, shown(me, chaser), chaser)


def test_a_craft_whose_own_contact_is_not_shown_counts_as_whole(craft, foe) -> None:
    me, chaser = craft(), foe(**BEHIND)
    assert attack.fights_back(me, Question(1000, (me,), (chaser,)), chaser)


def test_only_an_attack_notebook_names_the_enemy_it_fights(craft, foe, shown) -> None:
    """The same scratch bytes name craft 12 (signature 11) under attack's code
    and mean nothing under another behavior's."""
    scratch = attack.Memory(target=12, signature=11).write()
    for code, after in ((attack.CODE, 12), (3, None)):
        book = notebook.Notebook(code, attack.ATTACK, 7, 990, scratch).encode()
        me = craft(order_target_mask=0, notebook=book)
        found = attack.pursuer(me, shown(me, foe(**AHEAD)))
        assert (found and found.object_index) == after

"""Fence for attack: breaking off from a target, a big ship's hull among them.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; contact 0 is its own, contact 1 its order's target
(``attack_setup``).
"""

from __future__ import annotations

import pytest
from attack_setup import at
from attack_setup import BULK_FREIGHTER_SIZE
from attack_setup import CRUISER
from attack_setup import CRUISER_BOX
from attack_setup import FOE
from attack_setup import FREIGHTER
from attack_setup import ISD_BOX
from attack_setup import ME
from attack_setup import phase_of
from attack_setup import SSD_BOX
from attack_setup import STARSHIP
from attack_setup import TIER_SKILL

from jedimaster_ai import geometry
from jedimaster_ai import hull
from jedimaster_ai import tactics
from jedimaster_ai.behaviors import attack
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import Steer


@pytest.mark.parametrize(
    ("heading", "toward", "held"),
    [
        (0x0000, 0x0A00, 0xC000),
        (0x0000, 0xF600, 0x4000),
        (0x7000, 0x7400, 0x3000),
        (0x1000, 0x0400, 0x5000),
        (0xF800, 0x0400, 0xB800),
    ],
)
def test_too_close_ahead_it_breaks_off_away_from_the_target(
    craft, foe, ask, heading, toward, held
) -> None:
    first = ask(craft(yaw=heading), foe(**at(toward, 0x800)), tick=1000)
    assert phase_of(first) == attack.BREAK
    assert first.actions.steer.yaw == held
    assert first.actions.steer.pitch == LEVEL
    assert first.actions.throttle == attack.FULL_THROTTLE
    assert first.actions.lasers == (HOLD_FIRE, HOLD_FIRE)


def test_only_a_target_dead_ahead_lets_the_dice_pick_the_side(craft, foe, ask) -> None:
    assert attack.break_heading(0x1000, 0x1005, 77) == (0xD000, 77)
    assert attack.break_heading(0x1000, 0x0FFB, 77) == (0x5000, 77)
    for toward in (0x0FFC, 0x1000, 0x1004):
        held, state = attack.break_heading(0x1000, toward, 77)
        assert held in (0x5000, 0xD000) and state != 77
    sides = {
        ask(craft(random_seed=seed), foe(y=0x800)).actions.steer.yaw
        for seed in range(16)
    }
    assert sides == {0x4000, 0xC000}


def test_a_break_holds_until_the_craft_is_outside_the_clearance(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    first = ask(craft(), foe(x=0x200, y=0x800), tick=1000)
    held = ask(craft(notebook=first.notebook), foe(x=0x200, y=0xF00), tick=1700)
    assert held.notebook == first.notebook
    assert held.actions.steer.yaw == 0xC000
    assert held.actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    caplog.clear()
    out = ask(craft(notebook=held.notebook), foe(y=0x1000), tick=1716)
    assert phase_of(out) == attack.ATTACK
    assert out.actions.steer.yaw == 0
    assert (
        f'attack.break_end object={ME} target={FOE} reason="outside" ticks=716 '
        "distance=4096 clearance=4096"
    ) in caplog.text


def test_a_break_ends_at_its_limit_even_inside_the_clearance(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    first = ask(craft(), foe(x=0x200, y=0x800), tick=1000)
    held = ask(craft(notebook=first.notebook), foe(y=-0x800), tick=1999)
    assert held.actions.steer.yaw == 0xC000
    caplog.clear()
    ended = ask(craft(notebook=first.notebook), foe(y=-0x800), tick=2000)
    assert phase_of(ended) == attack.ATTACK
    assert ended.actions.steer.yaw == 0x8000
    assert (
        f'attack.break_end object={ME} target={FOE} reason="limit" ticks=1000 '
        "distance=2048 clearance=4096"
    ) in caplog.text


def test_a_break_ends_when_its_target_is_gone(craft, foe, ask, caplog) -> None:
    caplog.set_level("DEBUG")
    first = ask(craft(), foe(x=0x200, y=0x800), tick=1000)
    caplog.clear()
    gone = ask(craft(notebook=first.notebook), tick=1100)
    assert phase_of(gone) == attack.SEEK
    assert f"attack.break_lost object={ME} target={FOE} ticks=100" in caplog.text


def test_it_breaks_off_from_a_cruiser_4096_off_its_hull(craft, foe, ask) -> None:
    """Its stern, 9,343 behind its position, faces the craft: the break
    comes 4,096 off it (ruling A12), well inside the 17,932 a clearance
    from its position kept."""
    edge = 9343 + 0x1000
    cruiser = CRUISER | CRUISER_BOX
    assert phase_of(ask(craft(), foe(y=edge - 1, **cruiser))) == attack.BREAK
    assert phase_of(ask(craft(), foe(y=edge, **cruiser))) == attack.ATTACK


def test_a_break_from_a_cruiser_holds_until_4096_off_its_hull(
    craft, foe, ask, caplog
) -> None:
    caplog.set_level("DEBUG")
    cruiser = CRUISER | CRUISER_BOX
    first = ask(craft(), foe(y=9343 + 3000, **cruiser), tick=1000)
    assert phase_of(first) == attack.BREAK
    near = foe(y=9343 + 0x1000 - 1, **cruiser)
    held = ask(craft(notebook=first.notebook), near, tick=1016)
    assert held.notebook == first.notebook
    caplog.clear()
    out = ask(craft(notebook=held.notebook), foe(y=9343 + 0x1000, **cruiser), tick=1032)
    assert phase_of(out) == attack.ATTACK
    assert (
        f'attack.break_end object={ME} target={FOE} reason="outside" ticks=32 '
        "distance=4096 clearance=4096"
    ) in caplog.text


def test_a_blocked_break_heads_away_from_the_hull_and_the_other_craft(
    craft, foe, ask, caplog
) -> None:
    """The cruiser's flank lies 3,155 off to the left, its position behind
    on the left; the usual break to +x meets the second freighter. Away
    from the flank and the freighter alike is yaw 5,734; away from the
    position and the freighter it would be 434, nearly straight on."""
    caplog.set_level("DEBUG")
    cruiser = foe(x=-6000, y=-9000, **CRUISER, **CRUISER_BOX)
    other_at = (3000, -5196, 1500)
    other = foe(
        object_index=FOE + 1,
        **dict(zip("xyz", other_at, strict=True)),
        genus=FREIGHTER,
        size=BULK_FREIGHTER_SIZE,
    )
    decision = ask(craft(), cruiser, other)
    assert phase_of(decision) == attack.BREAK
    away = attack.escape((-3155, 0, 0), other_at)
    assert decision.actions.steer == Steer(*away, 0)
    assert away[0] == 5734
    assert f"blocked={FOE + 1}" in caplog.text


def test_a_break_turns_away_from_the_hulls_nearest_corner_not_its_position(
    craft, foe, ask
) -> None:
    """A Star Destroyer at yaw 0x6000, its position 0x9FC off the nose to
    the left and its nearest corner 2,075 off to the right: the break turns
    left, away from the corner; away from the position it would turn into
    it."""
    destroyer = foe(
        x=-10000,
        y=40000,
        yaw=0x6000,
        pitch=LEVEL,
        genus=STARSHIP,
        size=65396,
        **ISD_BOX,
    )
    near = hull.nearest(craft(), destroyer)
    assert near.distance == 2075
    assert geometry.bearing(near.dx, near.dy, near.dz)[0] == 0x1FFE
    decision = ask(craft(), destroyer)
    assert phase_of(decision) == attack.BREAK
    assert decision.actions.steer == Steer(0xC000, LEVEL, 0)


@pytest.mark.parametrize(
    ("toward", "held"), [(0x4000, 0xC000), (0xC000, 0x4000), (0xA000, 0x4000)]
)
def test_inside_a_bulky_targets_clearance_it_breaks_wherever_the_target_lies(
    craft, foe, ask, toward, held
) -> None:
    freighter = foe(**at(toward, 3000), genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    decision = ask(craft(), freighter)
    assert phase_of(decision) == attack.BREAK
    assert decision.actions.steer == Steer(held, LEVEL, 0)
    fighter = foe(**at(toward, 3000))
    assert phase_of(ask(craft(), fighter)) == attack.ATTACK


def test_a_break_another_freighters_clearance_blocks_heads_away_from_both(
    craft, foe, ask, caplog
) -> None:
    """The target behind on the left makes the usual break a turn to +x,
    where a second freighter sits 6,184 units off, behind on the right and a
    little above, inside its clearance; the way out, away from both, turns
    a little right, across neither, and dives a little."""
    target = foe(x=-2560, y=-451, genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    other = foe(
        object_index=FOE + 1,
        x=3000,
        y=-5196,
        z=1500,
        genus=FREIGHTER,
        size=BULK_FREIGHTER_SIZE,
    )
    me = craft()
    assert attack.break_heading(0, geometry.bearing(-2560, -451, 0)[0], 0)[0] == 0x4000
    caplog.set_level("DEBUG")
    decision = ask(me, target, other)
    assert phase_of(decision) == attack.BREAK
    yaw, pitch = attack.escape(tactics.offset(me, target), tactics.offset(me, other))
    assert decision.actions.steer == Steer(yaw, pitch, 0)
    assert pitch > LEVEL
    for contact in (target, other):
        toward = geometry.bearing(contact.x, contact.y, 0)[0]
        assert geometry.angle_between(yaw, toward) > geometry.QUARTER_TURN
    assert f"attack.break object={ME} target={FOE} " in caplog.text
    assert f"blocked={FOE + 1}" in caplog.text
    alone = ask(me, target)
    assert alone.actions.steer == Steer(0x4000, LEVEL, 0)
    assert "blocked=65535" in caplog.text


def test_a_break_a_big_ships_hull_blocks_heads_away_from_its_nearest_part(
    craft, foe, ask, caplog
) -> None:
    """The usual break turns to +x, where a Star Destroyer's bow lies 3,162
    units off, its position some 47,000 to the south-east: the way out
    heads away from the target and the bow's corner, not the ship's middle
    (ruling 94)."""
    target = foe(x=-2560, y=-451, genus=FREIGHTER, size=BULK_FREIGHTER_SIZE)
    # Its flank 3,000 east, its bow (the box's smallest y, ahead) 1,000 south.
    other = foe(
        object_index=FOE + 1,
        x=ISD_BOX["box_max"][0] + 3000,
        y=ISD_BOX["box_min"][1] - 1000,
        yaw=0,
        pitch=LEVEL,
        genus=STARSHIP,
        size=65396,
        **ISD_BOX,
    )
    me = craft()
    near = hull.nearest(me, other)
    assert (near.distance, near.dx, near.dy) == (3162, 3000, -1000)
    caplog.set_level("DEBUG")
    decision = ask(me, target, other)
    assert f"blocked={FOE + 1}" in caplog.text
    away = attack.escape(tactics.offset(me, target), (near.dx, near.dy, near.dz))
    assert decision.actions.steer == Steer(*away, 0)
    middle = attack.escape(tactics.offset(me, target), tactics.offset(me, other))
    assert geometry.angle_between(away[0], middle[0]) > 0x800


@pytest.mark.parametrize(
    ("target_at", "other_at", "held", "crossed"),
    [
        ((-2000, 2000, 0), (6000, 1000, 1500), 0x4000, "target"),
        ((6568, 4104, 0), (-5426, 4553, 0), 0xC000, "other"),
    ],
)
def test_an_escape_that_swings_the_nose_across_either_keeps_the_quarter_turn(
    craft, foe, ask, caplog, target_at, other_at, held, crossed
) -> None:
    """Away from both lies nearly behind, and the shortest turn there swings
    the nose across one of them: the target, ahead on the left with the
    other off on the right and a little above (AI5e's network bomber); or
    the other, ahead on the left with the target ahead on the right, the
    craft flying into the gap between them (the toy's column 12,000). The
    break keeps its quarter turn away from the target, and no craft is
    named as blocking it."""
    shape = {"genus": FREIGHTER, "size": BULK_FREIGHTER_SIZE}
    target = foe(**dict(zip("xyz", target_at, strict=True)), **shape)
    other = foe(
        object_index=FOE + 1, **dict(zip("xyz", other_at, strict=True)), **shape
    )
    me = craft()
    yaw, _ = attack.escape(tactics.offset(me, target), tactics.offset(me, other))
    swept = {"target": target_at, "other": other_at}[crossed]
    assert geometry.sweeps(0, yaw, geometry.bearing(*swept)[0])
    caplog.set_level("DEBUG")
    decision = ask(me, target, other)
    assert phase_of(decision) == attack.BREAK
    assert decision.actions.steer == Steer(held, LEVEL, 0)
    assert "blocked=65535" in caplog.text


def test_escape_points_away_from_both_unless_nearly_in_line() -> None:
    north, south = (0, 3000, 0), (0, -3000, 0)
    assert attack.escape(north, south) is None
    yaw, pitch = attack.escape(north, (3000, 0, 0))
    assert geometry.angle_between(yaw, 0xA000) <= 4
    assert pitch == LEVEL
    assert attack.escape(north, (0, 0, 3000))[1] > LEVEL


@pytest.mark.parametrize("tier", range(3))
@pytest.mark.parametrize(("box", "size"), [(ISD_BOX, 65396), (SSD_BOX, 327683)])
def test_every_tier_fires_on_a_star_destroyers_flank_20000_off(
    craft, foe, ask, tier, box, size
) -> None:
    """Issue #302: measured from its position, the lowest tier never fired
    at a Star Destroyer and no tier at a Super Star Destroyer; 20,000 off
    the flank lies inside even the lowest tier's 24,576."""
    flank = box["box_max"][0]
    ship = foe(
        y=flank + 20000, yaw=0x4000, pitch=LEVEL, genus=STARSHIP, size=size, **box
    )
    decision = ask(craft(skill=TIER_SKILL[tier]), ship)
    assert phase_of(decision) == attack.ATTACK
    assert decision.actions.lasers != (HOLD_FIRE, HOLD_FIRE)

"""Fence for the weave: its swerves, its end, its rest and its flare.

The craft flies at the origin facing +y (yaw 0), level, at tier 2 unless a
test says otherwise; craft 12 chases it. The numbers are written out, not
read from ``weave``, so a changed constant shows. The dice states are the
seeds 1 to 400.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import weave
from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.weave import Weave

ME = 4
CHASER = 12
SEEDS = range(1, 401)
FLARE_READY = {
    "countermeasure": 2,
    "countermeasures_left": 4,
    "countermeasure_working": 1,
    "countermeasure_cooldown": 0,
}


@pytest.fixture
def craft(view):
    def make(**changes):
        mine = {
            "object_index": ME,
            "yaw": 0,
            "pitch": LEVEL,
            "roll": 0,
            "skill": 0xFFFF,
        }
        return view(**(mine | changes))

    return make


def swerve_yaw(course: int, side: int) -> int:
    return (course + (0x3000 if side else -0x3000)) % 0x10000


def to_second_swerve(me, seed: int):
    """Return the weave, move and dice at the second swerve of a weave begun
    at tick 1000."""
    begun, _, state = weave.start(me, CHASER, Weave(), seed, 1000)
    return weave.fly(me, begun, True, state, 1472)


def test_a_weave_may_start_about_a_course_the_behavior_names(craft) -> None:
    begun, move, _ = weave.start(craft(yaw=0x1000), CHASER, Weave(), 7, 1000, 0x8000)
    assert begun.course == 0x8000
    assert move.steer.yaw == swerve_yaw(0x8000, begun.side)


def test_a_weave_starts_with_a_swerve_to_either_side_of_its_course(
    craft, caplog
) -> None:
    caplog.set_level("DEBUG")
    me = craft(yaw=0x1000, roll=0x0100)
    sides, pitches = set(), []
    for seed in SEEDS:
        begun, move, _ = weave.start(me, CHASER, Weave(rest_until=900), seed, 1000)
        assert begun.course == 0x1000 and begun.swerves == 1
        assert begun.swerve_ends == 1472 and begun.rest_until == 900
        assert move.steer.yaw == swerve_yaw(0x1000, begun.side)
        assert move.steer.roll == (0x4100 if begun.side else 0xC100)
        assert move.steer.pitch == begun.pitch
        assert angle_between(begun.pitch, LEVEL) <= 0x1800
        assert not move.flare
        sides.add(begun.side)
        pitches.append(begun.pitch)
    assert sides == {0, 1}
    assert max(pitches) - min(pitches) > 0x2000
    assert f"weave.start object={ME} threat={CHASER} course=4096 side=" in caplog.text


def test_a_swerve_holds_until_its_time_and_the_roll_keeps_going(craft) -> None:
    begun, first, state = weave.start(craft(), CHASER, Weave(), 7, 1000)
    flown, move, after = weave.fly(craft(roll=0x2345), begun, True, state, 1471)
    assert flown == begun and after == state
    assert move.steer.yaw == first.steer.yaw
    assert move.steer.pitch == first.steer.pitch
    assert move.steer.roll == (0x6345 if begun.side else 0xE345)
    assert not move.flare


def test_the_next_swerve_takes_the_other_side(craft, caplog) -> None:
    caplog.set_level("DEBUG")
    me = craft(yaw=0x8000)
    ends = set()
    for seed in SEEDS:
        begun, _, state = weave.start(me, CHASER, Weave(), seed, 1000)
        flown, move, _ = weave.fly(me, begun, True, state, 1472)
        assert flown.side == 1 - begun.side and flown.swerves == 2
        assert flown.course == 0x8000
        assert move.steer.yaw == swerve_yaw(0x8000, flown.side)
        assert 1472 + 472 <= flown.swerve_ends <= 1472 + 707
        ends.add(flown.swerve_ends)
    assert min(ends) < 1472 + 500 and max(ends) > 1472 + 680
    assert f"weave.swerve object={ME} swerve=2 side=" in caplog.text


def test_three_swerves_then_it_ends_and_rests(craft, caplog) -> None:
    caplog.set_level("DEBUG")
    me = craft()
    second, _, state = to_second_swerve(me, 11)
    third, _, state = weave.fly(me, second, True, state, second.swerve_ends)
    assert third.swerves == 3 and third.side == 1 - second.side
    held, move, _ = weave.fly(me, third, True, state, third.swerve_ends - 1)
    assert held == third and move is not None
    caplog.clear()
    tick = third.swerve_ends
    ended, move, after = weave.fly(me, third, True, state, tick)
    assert move is None and after == state
    assert ended == Weave(rest_until=tick + 1886)
    assert (
        f'weave.end object={ME} reason="limit" swerves=3 rest_until={tick + 1886}'
        in caplog.text
    )
    assert not weave.may_start(ended, tick + 1885)
    assert weave.may_start(ended, tick + 1886)


def test_a_clear_tail_ends_the_weave_at_once_without_rest(craft, caplog) -> None:
    caplog.set_level("DEBUG")
    begun, _, state = weave.start(craft(), CHASER, Weave(rest_until=600), 3, 1000)
    caplog.clear()
    ended, move, after = weave.fly(craft(), begun, False, state, 1100)
    assert move is None and after == state
    assert ended == Weave(rest_until=600)
    assert f'weave.end object={ME} reason="clear" swerves=1 rest_until=600' in (
        caplog.text
    )
    assert weave.may_start(ended, 1100)


def test_no_weave_starts_while_one_flies_or_the_craft_rests() -> None:
    assert weave.may_start(Weave(), 0)
    assert not weave.may_start(Weave(swerves=1), 5000)
    assert not weave.may_start(Weave(rest_until=3000), 2999)
    assert weave.may_start(Weave(rest_until=3000), 3000)


def test_a_top_pilot_drops_a_flare_on_about_one_swerve_in_four(craft, caplog) -> None:
    caplog.set_level("DEBUG")
    me = craft(**FLARE_READY)
    flares = 0
    for seed in SEEDS:
        caplog.clear()
        _, move, _ = to_second_swerve(me, seed)
        flares += move.flare
        assert f"flare={int(move.flare)} " in caplog.text
    assert 70 <= flares <= 130


@pytest.mark.parametrize(
    "changes",
    [
        {"skill": 0xBFFF},
        {"countermeasure": 1},
        {"countermeasure_cooldown": 5},
        {"countermeasures_left": 0},
        {"countermeasure_working": 0},
    ],
)
def test_only_a_top_pilot_with_a_flare_ready_drops_one(craft, changes) -> None:
    me = craft(**(FLARE_READY | changes))
    assert not any(to_second_swerve(me, seed)[1].flare for seed in SEEDS)


def test_tier_two_begins_at_skill_0xc000(craft) -> None:
    me = craft(**(FLARE_READY | {"skill": 0xC000}))
    assert any(to_second_swerve(me, seed)[1].flare for seed in SEEDS)


def test_the_state_fits_its_bytes() -> None:
    assert weave.STATE_BYTES == 14
    assert Weave.read(bytes(14)) == Weave()
    full = Weave(0xFFFF, 0x5800, 1, 3, 0xFFFFFFFF, 0x12345678)
    assert Weave.read(full.write()) == full
    assert full.write()[:6] == bytes([0xFF, 0xFF, 0x00, 0x58, 0x01, 0x03])


def test_a_chased_craft_weaves_across_its_course(craft) -> None:
    """A toy flight: the craft turns toward its steer at the slowest measured
    rate, a quarter turn in 420 ticks, thinking every 16 ticks while craft 12
    stays on its tail. Its heading crosses the course it began on at each
    swerve after the first, and the weave lasts 1,416 to 1,886 ticks."""
    turn_per_think = 0x4000 * 16 // 420
    for seed in range(1, 41):
        yaw, tick = 0, 1000
        flown, move, state = weave.start(craft(), CHASER, Weave(), seed, tick)
        sides_flown = []
        while move is not None:
            gap = (move.steer.yaw - yaw + 0x8000) % 0x10000 - 0x8000
            yaw = (yaw + max(-turn_per_think, min(turn_per_think, gap))) % 0x10000
            off = (yaw + 0x8000) % 0x10000 - 0x8000
            side = 1 if off > 0 else 0
            if off and (not sides_flown or sides_flown[-1] != side):
                sides_flown.append(side)
            tick += 16
            flown, move, state = weave.fly(craft(yaw=yaw), flown, True, state, tick)
        assert len(sides_flown) == 3
        assert 1000 + 1416 <= tick < 1000 + 1886 + 16

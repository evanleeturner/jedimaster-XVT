"""Fence for hull: how far a craft is from a starship's or platform's hull,
by the box around its model and its yaw, pitch and roll.

The boxes are the retail models' smallest and largest corners in the
model's axes, from the same 2026-10-13 opt_dump run as ``test_tactics.py``'s
sizes. The expected numbers are worked by hand from the engine's axes
(``hull``'s docstring): a level craft at yaw 0 has its side along +x, its
up along +z and its forward along +y, and the model's y runs backward.
The engine's own cases, its local-to-world turn over a table of angles,
come with the wire version that sends the box.
"""

from __future__ import annotations

import random

import pytest

from jedimaster_ai import geometry
from jedimaster_ai import hull
from jedimaster_ai import tactics
from jedimaster_ai.geometry import LEVEL

ISD_BOX = {"box_min": (-19140, -40940, -7132), "box_max": (19140, 24456, 12570)}
SSD_BOX = {"box_min": (-52487, -162468, -11526), "box_max": (52487, 165215, 13158)}
STARSHIP = 4
PLATFORM = 5
FREIGHTER = 3
# geometry.sine's stated error, in 65,536ths; an axis component sums two
# products of sines, each off by at most two such errors.
SINE_ERROR = 20
AXIS_ERROR = 4 * SINE_ERROR


@pytest.fixture
def me(view):
    return view(x=0, y=0, z=0, yaw=0, pitch=LEVEL, roll=0)


@pytest.fixture
def ship(contact):
    def make(**changes):
        theirs = {"x": 0, "y": 0, "z": 0, "yaw": 0, "pitch": LEVEL, "roll": 0}
        return contact(**(theirs | {"genus": STARSHIP} | ISD_BOX | changes))

    return make


def test_a_level_craft_at_yaw_0_has_the_engines_axes() -> None:
    assert hull.axes(0, LEVEL, 0) == ((65536, 0, 0), (0, 0, 65536), (0, 65536, 0))


def test_the_axes_stay_square_and_right_handed_at_any_angles() -> None:
    dice = random.Random(302)
    for _ in range(2000):
        side, up, forward = hull.axes(
            *(dice.randrange(geometry.TURN) for _ in range(3))
        )
        for axis in (side, up, forward):
            assert abs(geometry.distance(*axis) - 65536) <= AXIS_ERROR
        for first, second in ((side, up), (side, forward), (up, forward)):
            assert abs(hull._dot(first, second) >> 16) <= AXIS_ERROR
        across = (
            side[1] * forward[2] - side[2] * forward[1],
            side[2] * forward[0] - side[0] * forward[2],
            side[0] * forward[1] - side[1] * forward[0],
        )
        assert all(
            abs((c >> 16) - u) <= AXIS_ERROR for c, u in zip(across, up, strict=True)
        )


def test_a_craft_ahead_of_a_star_destroyer_is_measured_from_its_bow(me, ship) -> None:
    """The bow, the model's smallest y, lies 40,940 ahead of the position."""
    assert hull.nearest(me, ship(y=-60000)) == hull.Nearest(19060, 0, -19060, 0)


def test_a_craft_off_its_flank_is_measured_from_its_side(me, ship) -> None:
    assert hull.nearest(me, ship(x=-53143)) == hull.Nearest(34003, -34003, 0, 0)


def test_yaw_turns_the_bow(me, ship) -> None:
    """At yaw 0x4000 the ship's forward is +x."""
    assert hull.nearest(me, ship(x=-60000, yaw=0x4000)) == hull.Nearest(
        19060, -19060, 0, 0
    )


def test_pitch_turns_the_bow(me, ship) -> None:
    """At pitch 0 the ship points straight up."""
    assert hull.nearest(me, ship(z=-60000, pitch=0)) == hull.Nearest(
        19060, 0, 0, -19060
    )


def test_roll_turns_the_side(me, ship) -> None:
    """A quarter roll turns the side up and the up to -x: from +x the craft
    meets the box's floor, 7,132 under the position."""
    assert hull.nearest(me, ship(x=-50000)).distance == 30860
    assert hull.nearest(me, ship(x=-50000, roll=0x4000)).distance == 42868


def test_a_super_star_destroyers_flank_is_near_its_hull(me, ship) -> None:
    near = hull.nearest(me, ship(x=-72487, **SSD_BOX))
    assert near == hull.Nearest(20000, -20000, 0, 0)


def test_inside_the_box_it_is_0_from_the_hull_and_points_at_the_position(
    me, ship
) -> None:
    assert hull.nearest(me, ship(x=-100, y=-200, z=-300)) == hull.Nearest(
        0, -100, -200, -300
    )


@pytest.mark.parametrize("genus", [0, 1, FREIGHTER])
def test_any_other_craft_is_measured_from_its_position(me, ship, genus) -> None:
    assert not hull.by_hull(ship(genus=genus))
    assert hull.nearest(me, ship(x=-53143, genus=genus)) == hull.Nearest(
        53143, -53143, 0, 0
    )


@pytest.mark.parametrize("genus", [STARSHIP, PLATFORM])
def test_starships_and_platforms_are_measured_from_the_hull(ship, genus) -> None:
    assert hull.by_hull(ship(genus=genus))


def test_a_box_of_nothing_is_the_position(me, ship) -> None:
    empty = ship(x=-3000, y=-4000, box_min=(0, 0, 0), box_max=(0, 0, 0))
    assert hull.nearest(me, empty) == hull.Nearest(5000, -3000, -4000, 0)


def test_a_craft_keeps_4096_off_a_hull_and_its_clearance_off_anything_else(
    ship,
) -> None:
    assert hull.keep(ship()) == 4096
    freighter = ship(genus=FREIGHTER, size=4960)
    assert hull.keep(freighter) == tactics.clearance(freighter) == 7816

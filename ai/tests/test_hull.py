"""Fence for hull: how far a craft is from a starship's or platform's hull,
by the box around its model and its yaw, pitch and roll.

The boxes are the retail models' smallest and largest corners in the
model's axes, from the same 2026-10-13 opt_dump run as ``test_tactics.py``'s
sizes. The expected numbers are worked by hand from the engine's axes
(``hull``'s docstring): a level craft at yaw 0 has its side along +x, its
up along +z and its forward along +y, and the model's y runs backward.
The engine's own cases, its local-to-world turn over a table of poses,
are checked in ``test_hull_frame.py``.
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


def test_a_point_off_the_box_is_offset_from_its_nearest_point(ship) -> None:
    """In the model's axes: past a corner, off by its overhang on every axis;
    past one face, on that axis alone; inside, by nothing."""
    isd = ship()
    assert hull.off_box(isd, (19150, 24476, -7162)) == (10, 20, -30)
    assert hull.off_box(isd, (0, -41940, 0)) == (0, -1000, 0)
    assert hull.off_box(isd, (19140, -40940, 12570)) == (0, 0, 0)


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


# A cube 2,000 a side, for the headings into a box: its near face 9,000 off,
# each corner's angle off the line to it worked by hand as the arctangent of
# 1,000 over 9,000, 6.34 degrees, 1,154 in 65,536ths of a turn.
CUBE = {"box_min": (-1000, -1000, -1000), "box_max": (1000, 1000, 1000)}


@pytest.mark.parametrize(("x", "y", "yaw"), [(0, 10000, 0), (10000, 0, 0x4000)])
def test_the_headings_into_a_box_spread_to_its_corners(me, ship, x, y, yaw) -> None:
    """The pitch's spread falls 6 short: the corners and the nearest point
    stand for the face, whose steepest point, its top edge's middle, lies
    between them."""
    assert hull.seen(me, ship(x=x, y=y, **CUBE), 0) == hull.Seen(yaw, 1154, LEVEL, 1148)


def test_a_grown_box_spreads_the_headings_wider(me, ship) -> None:
    """Grown by 500, the corners lie 1,500 off the line, 8,500 ahead: the
    arctangent is 10.01 degrees, 1,822."""
    assert hull.seen(me, ship(y=10000, **CUBE), 500).yaw_edge == 1822


def test_inside_a_grown_box_the_headings_spread_a_quarter_turn(me, ship) -> None:
    near = hull.nearest(me, ship(y=1200, **CUBE))
    toward = geometry.bearing(near.dx, near.dy, near.dz)
    quarter = geometry.QUARTER_TURN
    assert hull.seen(me, ship(y=1200, **CUBE), 500) == hull.Seen(
        toward[0], quarter, toward[1], quarter
    )


def test_under_a_box_its_yaws_go_round_and_its_pitches_are_up(me, ship) -> None:
    """From under it, the yaws to a box go all the way round: every yaw; the
    pitches run from straight up to its near face's corners, 70.5 degrees
    above level."""
    seen = hull.seen(me, ship(z=5000, **CUBE), 0)
    assert seen.yaw_edge == hull.ALL_ROUND
    assert (seen.pitch - seen.pitch_edge, seen.pitch + seen.pitch_edge) == (0, 3542)


def test_over_a_box_its_yaws_go_round_and_its_pitches_are_down(me, ship) -> None:
    """From over it, as from under: every yaw, and the pitches from its near
    face's corners, 70.5 degrees below level (the arctangent of 4,000 over
    1,414, 12,842 off level), to straight down."""
    seen = hull.seen(me, ship(z=-5000, **CUBE), 0)
    assert seen.yaw_edge == hull.ALL_ROUND
    assert (seen.pitch - seen.pitch_edge, seen.pitch + seen.pitch_edge) == (
        29226,
        32768,
    )


def test_inside_a_grown_box_over_the_box_its_yaws_go_round(me, ship) -> None:
    """500 over the box's top, inside it grown by 1,000: every yaw, and a
    quarter turn either side of straight down."""
    seen = hull.seen(me, ship(z=-1500, **CUBE), 1000)
    assert (seen.yaw_edge, seen.pitch, seen.pitch_edge) == (
        hull.ALL_ROUND,
        geometry.HALF_TURN,
        geometry.QUARTER_TURN,
    )


def test_inside_the_box_itself_the_headings_spread_a_quarter_turn(me, ship) -> None:
    """500 under the box's top, inside the box (a hangar launch): its nearest
    part is the position, straight down, and a quarter turn either side of
    it stands, though the box's corners lie all round. Straight down has yaw
    a quarter turn (``geometry``)."""
    quarter = geometry.QUARTER_TURN
    seen = hull.seen(me, ship(z=-500, **CUBE), 1000)
    assert seen == hull.Seen(quarter, quarter, geometry.HALF_TURN, quarter)

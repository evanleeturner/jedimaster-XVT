"""Fence for geometry: the engine's angle conventions, in integers."""

from __future__ import annotations

import math
import random

import pytest

from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import arctangent
from jedimaster_ai.geometry import bearing
from jedimaster_ai.geometry import distance
from jedimaster_ai.geometry import forward
from jedimaster_ai.geometry import LEVEL
from jedimaster_ai.geometry import signed
from jedimaster_ai.geometry import sine
from jedimaster_ai.geometry import sweeps


def exact_angle(y: int, x: int) -> int:
    return round(math.atan2(y, x) * 0x10000 / (2 * math.pi)) % 0x10000


@pytest.mark.parametrize("scale", [1, 10, 1000, 1 << 20, 1 << 31])
def test_the_arctangent_is_within_4_units_of_the_exact_angle(scale: int) -> None:
    dice = random.Random(scale)
    for _ in range(5000):
        x, y = dice.randint(-scale, scale), dice.randint(-scale, scale)
        if x or y:
            assert angle_between(arctangent(y, x), exact_angle(y, x)) <= 4, (x, y)


@pytest.mark.parametrize(
    ("offset", "expected"),
    [
        ((0, 100, 0), (0, LEVEL)),
        ((100, 0, 0), (0x4000, LEVEL)),
        ((0, -100, 0), (0x8000, LEVEL)),
        ((-100, 0, 0), (0xC000, LEVEL)),
        ((0, 0, 100), (0x4000, 0)),
        ((0, 0, -100), (0x4000, 0x8000)),
        ((0, 0, 0), (0x4000, LEVEL)),
    ],
)
def test_the_bearing_follows_the_engines_axes(offset, expected) -> None:
    assert bearing(*offset) == expected


def test_a_climb_of_45_degrees_ahead_is_pitch_0x2000() -> None:
    yaw, pitch = bearing(0, 5000, 5000)
    assert yaw == 0
    assert angle_between(pitch, 0x2000) <= 4


def test_the_answers_are_integers() -> None:
    assert all(type(value) is int for value in (*bearing(7, -3, 2), distance(7, -3, 2)))


def test_distance_is_the_length_rounded_down() -> None:
    assert distance(3, 4, 12) == 13
    assert distance(1, 1, 1) == 1
    assert distance(-(1 << 31), 0, 0) == 1 << 31


@pytest.mark.parametrize(
    ("first", "second", "apart"),
    [(10, 65530, 16), (65530, 10, 16), (0, 0x8000, 0x8000), (0x4000, 0x4000, 0)],
)
def test_angles_are_apart_the_short_way_round(first, second, apart) -> None:
    assert angle_between(first, second) == apart


def test_sweeps_names_a_turn_across_the_bearing_the_shortest_way() -> None:
    assert sweeps(0, 0x3000, 0x1000)
    assert sweeps(0, 0xD000, 0xF000)
    assert not sweeps(0, 0x3000, 0xF000)
    assert not sweeps(0, 0x3000, 0x3000)
    assert not sweeps(0x1000, 0x3000, 0x1000)
    # Past a half turn the shortest way is the other way round.
    assert sweeps(0, 0x9000, 0xC000)
    assert not sweeps(0, 0x9000, 0x4000)


def test_signed_runs_from_minus_a_half_turn() -> None:
    assert [signed(a) for a in (0, 0x7FFF, 0x8000, 0xFFFF)] == [
        0,
        0x7FFF,
        -0x8000,
        -1,
    ]


def test_sine_is_odd_and_within_20_of_the_exact_sine() -> None:
    assert [sine(a) for a in (0, 0x4000, 0x8000, 0xC000)] == [0, 0x10000, 0, -0x10000]
    for angle in range(0, 0x10000, 0x101):
        exact = round(0x10000 * math.sin(angle * 2 * math.pi / 0x10000))
        assert abs(sine(angle) - exact) <= 20
        assert sine(-angle % 0x10000) == -sine(angle)


@pytest.mark.parametrize(
    ("yaw", "pitch", "expected"),
    [
        (0, LEVEL, (0, 0x10000, 0)),
        (0x4000, LEVEL, (0x10000, 0, 0)),
        (0x8000, LEVEL, (0, -0x10000, 0)),
        (0xC000, LEVEL, (-0x10000, 0, 0)),
        (0x1234, 0, (0, 0, 0x10000)),
        (0x1234, 0x8000, (0, 0, -0x10000)),
    ],
)
def test_forward_follows_the_engines_axes(yaw, pitch, expected) -> None:
    assert forward(yaw, pitch) == expected


# Two sines of up to 20 65,536ths each make the length; the arctangent's 4
# units and a sine's 20 65,536ths of a radian, under 4 units, make an angle.
FORWARD_LENGTH_ERROR = 40
FORWARD_ANGLE_ERROR = 8


def test_forward_points_back_along_the_bearing() -> None:
    rng = random.Random(7)
    for _ in range(500):
        yaw, pitch = rng.randrange(0x10000), rng.randrange(0x400, 0x7C00)
        x, y, z = forward(yaw, pitch)
        assert abs(math.hypot(x, y, z) - 0x10000) <= FORWARD_LENGTH_ERROR
        back_yaw, back_pitch = bearing(x, y, z)
        assert angle_between(back_yaw, yaw) <= FORWARD_ANGLE_ERROR
        assert angle_between(back_pitch, pitch) <= FORWARD_ANGLE_ERROR

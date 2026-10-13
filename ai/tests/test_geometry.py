"""Fence for geometry: the engine's angle conventions, in integers."""

from __future__ import annotations

import math
import random

import pytest

from jedimaster_ai.geometry import angle_between
from jedimaster_ai.geometry import arctangent
from jedimaster_ai.geometry import bearing
from jedimaster_ai.geometry import distance
from jedimaster_ai.geometry import LEVEL


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

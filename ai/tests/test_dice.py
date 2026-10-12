"""Fence for dice: the same numbers from the same state, on every machine.

The pinned numbers were harvested once, 2026-10-12, and must never change: a
change would make a saved game's craft choose differently.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import dice


def test_step_gives_the_pinned_numbers() -> None:
    state, seen = 1, []
    for _ in range(3):
        state = dice.step(state)
        seen.append(state)
    assert seen == [270369, 67634689, 2647435461]


def test_seed_gives_the_pinned_states() -> None:
    assert dice.seed(-3, 4) == 2852282485
    assert dice.seed(0, 0) == 2654435761
    assert dice.seed(0, 1) == 1013904226


def test_seed_keeps_pairs_apart() -> None:
    pairs = [(seed, index) for seed in range(-4, 5) for index in range(64)]
    assert len({dice.seed(*pair) for pair in pairs}) == len(pairs)


def test_seed_shares_its_one_state_at_index_ffff() -> None:
    assert dice.seed(-1, 0xFFFF) == dice.seed(-2, 0xFFFF) == dice.SHARED != 0


def test_roll_gives_the_pinned_numbers() -> None:
    state, seen = dice.seed(-3, 4), []
    for _ in range(5):
        value, state = dice.roll(state, 6)
        seen.append(value)
    assert seen == [1, 3, 3, 5, 1]


def test_states_stay_in_range() -> None:
    state = 1
    for _ in range(10_000):
        state = dice.step(state)
        assert 1 <= state <= dice.MASK


def test_roll_needs_a_side() -> None:
    with pytest.raises(ValueError):
        dice.roll(1, 0)

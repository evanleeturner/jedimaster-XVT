"""Fence for lord: the Sith Lord's own rules, at skill level 7 alone.

Which level is a Lord's, and the books that send a Lord home: every home
book but the abort, logged when the abort is set aside. The callers' tests
hold the rest: ``test_attack_fight_back.py`` and ``test_home.py`` the fight
at any hull, ``test_progress.py`` and ``test_dropoff.py`` the abort,
``test_warheads.py`` the unguided shot at any craft.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import lord
from jedimaster_ai.world import Books

# The books that send a craft home (``progress.HOME_BOOKS``).
HOME = Books.ABORTED | Books.DEPARTURE_DUE | Books.ORDERS_DONE


@pytest.mark.parametrize(("level", "is_lord"), [(5, False), (6, False), (7, True)])
def test_only_skill_level_7_is_a_lord(view, level, is_lord) -> None:
    assert lord.is_lord(view(skill_level=level)) is is_lord


@pytest.mark.parametrize("level", [0, 5, 6])
def test_every_other_rank_keeps_every_home_book(view, level) -> None:
    assert lord.home_books(view(skill_level=level), HOME) == HOME


def test_a_lord_sets_its_abort_aside_and_keeps_the_rest(view, caplog) -> None:
    caplog.set_level("DEBUG")
    craft = view(skill_level=7)
    assert lord.home_books(craft, HOME) == Books.DEPARTURE_DUE | Books.ORDERS_DONE
    assert f"lord.stays object={craft.object_index} books={int(HOME)}" in caplog.text


def test_a_lord_with_only_its_abort_has_no_home_book(view) -> None:
    assert lord.home_books(view(skill_level=7), Books.ABORTED) == 0


def test_a_lord_with_no_abort_logs_nothing(view, caplog) -> None:
    caplog.set_level("DEBUG")
    books = Books.DEPARTURE_DUE
    assert lord.home_books(view(skill_level=7), books) == books
    assert "lord.stays" not in caplog.text

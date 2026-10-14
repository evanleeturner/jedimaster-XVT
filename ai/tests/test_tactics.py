"""Fence for tactics: how far a craft keeps from another, which craft are bulky,
and which a craft may fight.

The sizes are measured, not chosen: on 2026-10-13 the engine's ``opt_dump``
(its ``file`` kind) read the retail models, and each model's box runs from
the smallest to the largest corner over its mesh descriptors. The longest
side is the size the engine sends; the farthest corner is how far the hull
may reach from the craft's position.
"""

from __future__ import annotations

import pytest

from jedimaster_ai import tactics
from jedimaster_ai.world import Question

# Model file: (longest side, farthest box corner from the position).
SMALL_MODELS = {
    "tieftr": (327, 265),
    "xwing": (531, 427),
    "tiebmb": (405, 341),
    "shuttle": (1400, 1078),
}
BULKY_MODELS = {
    "freight": (4960, 3069),
    "contain": (3100, 1795),
    "corv": (6011, 3716),
    "friga": (10368, 6949),
    "crusb": (18449, 9976),
    "crusc": (17879, 10928),
    "plata": (10250, 7620),
    "cal": (53188, 30903),
    "isd": (65396, 46909),
    "ssd": (327683, 173850),
}


@pytest.mark.parametrize("model", sorted(SMALL_MODELS | BULKY_MODELS))
def test_the_clearance_reaches_past_every_measured_hull(contact, model) -> None:
    size, corner = (SMALL_MODELS | BULKY_MODELS)[model]
    assert tactics.clearance(contact(size=size)) > corner


@pytest.mark.parametrize("model", sorted(BULKY_MODELS))
def test_a_bulky_hull_is_cleared_by_the_whole_margin(contact, model) -> None:
    size, corner = BULKY_MODELS[model]
    keep = tactics.clearance(contact(size=size))
    assert keep - tactics.CLEARANCE_MARGIN > corner


def test_a_fighter_is_kept_from_about_as_far_as_before_sizes(contact) -> None:
    assert tactics.clearance(contact(size=0)) == 0x1000
    assert tactics.clearance(contact(size=327)) == 4341


@pytest.mark.parametrize("genus", range(17))
def test_freighters_starships_and_platforms_are_bulky(contact, genus) -> None:
    assert tactics.is_bulky(contact(genus=genus)) == (genus in (3, 4, 5))


def test_enemies_are_every_hostile_craft_it_may_fight_in_order(view, contact) -> None:
    """The engine's mask marks contacts 0, 1, 2 and 4; 0 is the craft's own,
    2 is exploding (kind 4) and 4 disabled (kind 2); 3 is not marked."""
    craft = view(object_index=4, enemy_mask=0b10111)
    contacts = (
        contact(object_index=4),
        contact(object_index=20),
        contact(object_index=21, object_kind=4),
        contact(object_index=22),
        contact(object_index=23, object_kind=2),
    )
    found = tactics.enemies(craft, Question(1, (craft,), contacts))
    assert [c.object_index for c in found] == [20, 23]

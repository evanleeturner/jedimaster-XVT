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


def _command_question(contact, *indices: int) -> Question:
    """Contacts at positions 1, 2, ... with these object indices, the craft's
    own (4) first."""
    own = contact(object_index=4)
    return Question(1, (), (own, *(contact(object_index=i) for i in indices)))


def test_a_commanded_enemy_is_the_only_target(view, contact) -> None:
    question = _command_question(contact, 20, 21, 22)
    craft = view(enemy_mask=0b1110, command_target=22)
    targets = list(question.contacts[1:2])
    assert [c.object_index for c in tactics.commanded(craft, question, targets)] == [22]


@pytest.mark.parametrize("command_target", [0xFFFF, 4, 23, 21])
def test_a_command_target_that_is_no_enemy_shown_leaves_the_targets(
    view, contact, command_target
) -> None:
    """No command (bug #50's 0xFFFF), the craft itself, a craft not shown, or
    one the engine does not mark hostile (21, here)."""
    question = _command_question(contact, 20, 21)
    craft = view(enemy_mask=0b010, command_target=command_target)
    targets = list(question.contacts[1:2])
    assert tactics.commanded(craft, question, targets) == targets


def test_only_the_ignored_craft_leaves_the_targets(view, contact) -> None:
    """Matched by object index, not by place in the list (bug #51), and the
    craft after it stay (bug #52)."""
    question = _command_question(contact, 30, 1, 31, 32)
    craft = view(enemy_mask=0b11110, avoid_target=31)
    kept = tactics.commanded(craft, question, list(question.contacts[1:]))
    assert [c.object_index for c in kept] == [30, 1, 32]


# The engine's skill by AI level, 0 to 7 (ai_brain.c), and the tier each takes.
@pytest.mark.parametrize(
    ("level", "skill", "tier"),
    [
        (0, 0x0000, 0),
        (1, 0x4000, 0),
        (2, 0x8000, 1),
        (3, 0xC000, 2),
        (4, 0xFFFF, 2),
        (5, 0xFFFF, 2),
        (6, 0xFFFF, 3),
        (7, 0xFFFF, 4),
    ],
)
def test_the_sith_levels_take_the_two_tiers_above_a_jedi(
    view, level, skill, tier
) -> None:
    assert tactics.skill_tier(view(skill=skill, skill_level=level)) == tier

"""The seven game options: their table, their words, their way into a description.

Purpose:
    Prove the table agrees with the launch schema and the control list,
    that texts and values convert both ways, that a stored value matches by
    type, and that an easy cheat outside a campaign is sent as easy.

Flow:
    Read the table and the schemas; call ``parse_value``, ``text_of``,
    ``parse_setting`` and ``for_description``.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_launch_options.py``
"""

from __future__ import annotations

import logging

import pytest

from jedimaster.launch.model import build_description
from jedimaster.launch.options import AS_GAME
from jedimaster.launch.options import CHEAT_NOTE
from jedimaster.launch.options import for_description
from jedimaster.launch.options import OPTIONS
from jedimaster.launch.options import parse_setting
from jedimaster.launch.options import parse_value
from jedimaster.launch.options import text_of
from jedimaster.launch.options import words_of
from jedimaster.launch.run import errors
from jedimaster.launch.schema import launch_schema
from jedimaster.page.control import parse_request
from jedimaster.page.protocol import OPTION_NAMES
from jedimaster.page.schema import control_schema

logger = logging.getLogger(__name__)


def test_the_table_lists_the_seven_options_the_control_list_names():
    assert tuple(OPTIONS) == OPTION_NAMES
    assert len(OPTION_NAMES) == 7


def test_every_choice_value_fits_the_launch_schema():
    properties = launch_schema()["properties"]["settings"]["properties"]
    assert set(properties) == set(OPTIONS)
    for name, spec in OPTIONS.items():
        for choice in spec.choices:
            document = {
                "schema_version": 1,
                "mode": "single_player",
                "game_type": "campaign",
                "side": "rebel",
                "mission": "campaign/a.cmp",
                "pilot": "Ace",
                "settings": {name: choice.value},
            }
            assert errors(document, launch_schema()) == [], (name, choice)


def test_every_choice_text_is_a_value_the_control_list_allows():
    defs = control_schema()["$defs"]["GameOptions"]["properties"]
    for name, spec in OPTIONS.items():
        listed = defs[name]["anyOf"][0]["enum"]
        assert listed == [c.text for c in spec.choices], name


def test_the_time_limit_has_none_default_and_one_to_twenty_minutes():
    texts = [c.text for c in OPTIONS["mission_time_limit"].choices]
    assert texts == ["none", "default", *[str(n) for n in range(1, 21)]]
    assert parse_value("mission_time_limit", "7") == 7
    assert parse_value("mission_time_limit", "none") == "none"


@pytest.mark.parametrize(
    "name, text, value",
    [
        ("difficulty", "hard", "hard"),
        ("collisions", "off", False),
        ("craft_jumping", "on", True),
        ("battle_length", "3", 3),
        ("continue_sequence", "restart", "restart"),
    ],
)
def test_text_and_value_convert_both_ways(name, text, value):
    assert parse_value(name, text) == value
    assert text_of(name, value) == text


def test_a_switch_is_not_a_number():
    # True == 1 in Python; a stored True must not read as the 1-minute limit.
    assert text_of("mission_time_limit", True) is None
    assert text_of("collisions", 1) is None
    assert text_of("battle_length", True) is None


@pytest.mark.parametrize(
    "name, text",
    [("difficulty", "impossible"), ("nothing", "on"), ("collisions", AS_GAME)],
)
def test_an_unknown_name_or_choice_is_refused(name, text):
    with pytest.raises(ValueError):
        parse_value(name, text)


def test_words_say_as_the_game_has_it_for_an_unset_option():
    assert words_of("difficulty", None) == "As the game has it"
    assert words_of("difficulty", "hard") == "Hard"


def test_a_setting_argument_is_name_equals_value():
    assert parse_setting("collisions=off") == ("collisions", False)
    with pytest.raises(ValueError):
        parse_setting("collisions")
    with pytest.raises(ValueError):
        parse_setting("collisions=maybe")


def test_an_easy_cheat_outside_a_campaign_is_sent_as_easy_with_a_note():
    settings, note = for_description(
        {"difficulty": "easy_cheat", "collisions": False}, "melee"
    )
    assert settings == {"difficulty": "easy", "collisions": False}
    assert note == CHEAT_NOTE


def test_an_easy_cheat_in_a_campaign_is_kept_and_other_options_pass_through():
    settings, note = for_description({"difficulty": "easy_cheat"}, "campaign")
    assert settings == {"difficulty": "easy_cheat"} and note is None
    settings, note = for_description({"difficulty": "hard"}, "melee")
    assert settings == {"difficulty": "hard"} and note is None


def test_for_description_does_not_change_what_it_was_given():
    stored = {"difficulty": "easy_cheat"}
    for_description(stored, "melee")
    assert stored == {"difficulty": "easy_cheat"}


def test_an_unset_option_is_left_out_of_the_description():
    settings, _ = for_description({}, "melee")
    description = build_description("melee", "none", "a.tie", "Ace", settings=settings)
    assert "settings" not in description


def test_the_control_list_refuses_a_value_off_the_table():
    from json import dumps

    text = dumps(
        {
            "id": 1,
            "command": "options.set",
            "args": {"player": "7", "name": "difficulty", "value": "impossible"},
        }
    )
    assert parse_request(text).code == "bad_arguments"

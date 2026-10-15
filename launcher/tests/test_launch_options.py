"""The seven game options: their table, their words, their way into a description.

Purpose:
    Prove the table agrees with the launch schema and the control list,
    that texts and values convert both ways, that a stored value matches by
    type, that each option has its default and every battle holds all seven,
    and that an easy cheat outside a campaign is sent as easy.

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
from jedimaster.launch.options import CHEAT_NOTE
from jedimaster.launch.options import current_text
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


def test_the_table_lists_the_eight_options_the_control_list_names():
    assert tuple(OPTIONS) == OPTION_NAMES
    assert len(OPTION_NAMES) == 8 and OPTION_NAMES[-1] == "sith"


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
        listed = defs[name]["enum"]
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
    [("difficulty", "impossible"), ("nothing", "on"), ("collisions", "none")],
)
def test_an_unknown_name_or_choice_is_refused(name, text):
    with pytest.raises(ValueError):
        parse_value(name, text)


def test_words_name_a_choice_and_refuse_what_is_no_choice():
    assert words_of("difficulty", "hard") == "Hard"
    with pytest.raises(ValueError):
        words_of("difficulty", None)
    with pytest.raises(ValueError):
        words_of("nothing", "on")


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
    assert settings["difficulty"] == "easy" and settings["collisions"] is False
    assert note == CHEAT_NOTE


def test_an_easy_cheat_in_a_campaign_is_kept_and_other_options_pass_through():
    settings, note = for_description({"difficulty": "easy_cheat"}, "campaign")
    assert settings["difficulty"] == "easy_cheat" and note is None
    settings, note = for_description({"difficulty": "hard"}, "melee")
    assert settings["difficulty"] == "hard" and note is None


def test_for_description_does_not_change_what_it_was_given():
    stored = {"difficulty": "easy_cheat"}
    for_description(stored, "melee")
    assert stored == {"difficulty": "easy_cheat"}


DEFAULTS = {
    "difficulty": "medium",
    "collisions": "on",
    "craft_jumping": "on",
    "random_setup": "off",
    "battle_length": "3",
    "mission_time_limit": "default",
    "continue_sequence": "continue",
    "sith": "none",
}


@pytest.mark.parametrize("name, text", sorted(DEFAULTS.items()))
def test_each_option_has_its_default(name, text):
    assert OPTIONS[name].default == text
    assert text in [c.text for c in OPTIONS[name].choices]
    assert current_text(name, {}) == text


def test_a_set_value_beats_the_default_and_an_unset_one_keeps_it():
    stored = {"difficulty": "hard", "collisions": False}
    assert current_text("difficulty", stored) == "hard"
    assert current_text("collisions", stored) == "off"
    assert current_text("craft_jumping", stored) == "on"


def test_every_description_holds_all_eight_options_but_sith_when_none():
    settings, note = for_description({}, "melee")
    assert set(settings) == set(OPTIONS) - {"sith"} and note is None
    assert settings == {
        "difficulty": "medium",
        "collisions": True,
        "craft_jumping": True,
        "random_setup": False,
        "battle_length": 3,
        "mission_time_limit": "default",
        "continue_sequence": "continue",
    }
    settings, _ = for_description({"battle_length": 4}, "campaign")
    assert set(settings) == set(OPTIONS) - {"sith"} and settings["battle_length"] == 4
    for sith in ("apprentices", "lord"):
        settings, _ = for_description({"sith": sith}, "melee")
        assert set(settings) == set(OPTIONS) and settings["sith"] == sith
    settings, _ = for_description({"sith": "none"}, "melee")
    assert "sith" not in settings


def test_the_sith_option_has_three_choices_in_words_and_defaults_to_none():
    spec = OPTIONS["sith"]
    assert [(c.text, c.value, c.words) for c in spec.choices] == [
        ("none", "none", "None"),
        ("apprentices", "apprentices", "Apprentices"),
        ("lord", "lord", "A Lord"),
    ]
    assert spec.default == "none" and spec.words == "Sith"
    assert parse_setting("sith=lord") == ("sith", "lord")


def test_a_description_with_the_defaults_carries_seven_settings_and_no_sith():
    settings, _ = for_description({}, "melee")
    description = build_description("melee", "none", "a.tie", "Ace", settings=settings)
    assert set(description["settings"]) == set(OPTIONS) - {"sith"}


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

"""The settings file: where it lives, what it holds, how it breaks.

Purpose:
    Prove the settings file's place on each operating system, that a change
    is written and read back by a new store, that a missing file gives the
    defaults, and that a broken file gives the defaults and a WARNING and
    stays as it is until a change.

Flow:
    ``default_settings_path`` with a made-up platform and environment;
    ``SettingsStore`` over files in a temporary folder.

Invariants:
    - No game data; nothing outside the temporary folder is written.

Call:
    ``pytest tests/test_page_settings.py``
"""

from __future__ import annotations

import json
import logging
from pathlib import Path

import pytest

from jedimaster.page.settings import default_settings_path
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)

DEFAULTS = {"art_scaling": "whole_pixels"}


def test_path_on_linux_uses_xdg_config_home():
    path = default_settings_path("linux", {"XDG_CONFIG_HOME": "/x/cfg"})
    assert path == Path("/x/cfg/jedimaster/settings.json")


def test_path_on_linux_falls_back_to_dot_config():
    path = default_settings_path("linux", {})
    assert path == Path.home() / ".config" / "jedimaster" / "settings.json"
    assert default_settings_path("linux", {"XDG_CONFIG_HOME": ""}) == path


def test_path_on_macos():
    path = default_settings_path("darwin", {"XDG_CONFIG_HOME": "/ignored"})
    expected = Path.home() / "Library/Application Support/jedimaster/settings.json"
    assert path == expected


def test_path_on_windows_uses_appdata():
    path = default_settings_path("win32", {"APPDATA": "C:/Users/p/AppData/Roaming"})
    assert path == Path("C:/Users/p/AppData/Roaming/jedimaster/settings.json")


def test_missing_file_gives_defaults_without_a_warning(tmp_path, caplog):
    with caplog.at_level(logging.WARNING):
        store = SettingsStore(tmp_path / "none" / "settings.json")
    assert store.get() == DEFAULTS
    assert not store.path.exists()
    assert caplog.text == ""


def test_a_file_that_is_not_text_gives_defaults_and_warns(tmp_path, caplog):
    path = tmp_path / "s.json"
    path.write_bytes(b"\xff\xfe{")
    with caplog.at_level(logging.WARNING):
        assert SettingsStore(path).get() == DEFAULTS
    assert "cannot read the settings file" in caplog.text


def test_a_change_is_written_and_read_back(tmp_path):
    path = tmp_path / "deep" / "settings.json"
    assert SettingsStore(path).set("art_scaling", "sharp_bilinear") is True
    assert json.loads(path.read_text(encoding="utf-8")) == {
        "art_scaling": "sharp_bilinear"
    }
    assert SettingsStore(path).get() == {"art_scaling": "sharp_bilinear"}
    assert not list(path.parent.glob("*.tmp"))


def test_get_returns_a_copy(tmp_path):
    store = SettingsStore(tmp_path / "s.json")
    store.get()["art_scaling"] = "engine_fit"
    assert store.get() == DEFAULTS


@pytest.mark.parametrize(
    "text", ["not json", "[1]", '"x"', "", '{"art_scaling": "stretched"}', "{"]
)
def test_broken_file_gives_defaults_warns_and_stays(tmp_path, caplog, text):
    path = tmp_path / "s.json"
    path.write_text(text, encoding="utf-8")
    with caplog.at_level(logging.WARNING):
        store = SettingsStore(path)
    assert store.get() == DEFAULTS
    assert path.read_text(encoding="utf-8") == text
    assert "settings file" in caplog.text


def test_a_set_replaces_a_broken_file(tmp_path):
    path = tmp_path / "s.json"
    path.write_text("not json", encoding="utf-8")
    SettingsStore(path).set("art_scaling", "engine_fit")
    assert SettingsStore(path).get() == {"art_scaling": "engine_fit"}


def test_unknown_names_in_the_file_are_ignored(tmp_path):
    path = tmp_path / "s.json"
    path.write_text('{"art_scaling": "engine_fit", "volume": 3}', encoding="utf-8")
    assert SettingsStore(path).get() == {"art_scaling": "engine_fit"}


@pytest.mark.parametrize(
    ("name", "value"),
    [("volume", "high"), ("art_scaling", "stretched"), ("art_scaling", "")],
)
def test_set_off_the_list_raises_and_changes_nothing(tmp_path, name, value):
    store = SettingsStore(tmp_path / "s.json")
    with pytest.raises(ValueError, match="not a setting"):
        store.set(name, value)
    assert store.get() == DEFAULTS


def test_unwritable_file_warns_and_keeps_the_value(tmp_path, caplog):
    blocker = tmp_path / "blocker"
    blocker.write_text("a file where a folder should be", encoding="utf-8")
    store = SettingsStore(blocker / "settings.json")
    with caplog.at_level(logging.WARNING):
        assert store.set("art_scaling", "engine_fit") is False
    assert store.get() == {"art_scaling": "engine_fit"}
    assert "cannot write the settings file" in caplog.text


# ---- each player's game options ------------------------------------------


def test_options_start_empty_and_are_per_player(tmp_path):
    store = SettingsStore(tmp_path / "s.json")
    assert store.get_options("7") == {}
    assert store.options == {}, "reading a player's options makes no entry"
    store.set_option("7", "difficulty", "hard")
    assert store.get_options("7") == {"difficulty": "hard"}
    assert store.get_options("8") == {}


def test_the_file_holds_only_the_options_that_are_set(tmp_path):
    store = SettingsStore(tmp_path / "s.json")
    assert store.set_option("7", "collisions", False) is True
    store.set_option("7", "battle_length", 3)
    on_disk = json.loads(store.path.read_text())
    assert on_disk == {
        "art_scaling": "whole_pixels",
        "game_options": {"7": {"collisions": False, "battle_length": 3}},
    }
    store.set_option("7", "collisions", None)
    assert json.loads(store.path.read_text())["game_options"] == {
        "7": {"battle_length": 3}
    }
    store.set_option("7", "battle_length", None)
    assert "game_options" not in json.loads(store.path.read_text())


def test_options_and_settings_share_the_file_without_losing_each_other(tmp_path):
    store = SettingsStore(tmp_path / "s.json")
    store.set_option("7", "difficulty", "easy")
    store.set("art_scaling", "engine_fit")
    again = SettingsStore(store.path)
    assert again.get() == {"art_scaling": "engine_fit"}
    assert again.get_options("7") == {"difficulty": "easy"}


def test_the_write_is_atomic_and_leaves_no_temporary_file(tmp_path):
    store = SettingsStore(tmp_path / "config" / "s.json")
    store.set_option("7", "difficulty", "easy")
    assert sorted(p.name for p in store.path.parent.iterdir()) == ["s.json"]


@pytest.mark.parametrize(
    "player, name, value",
    [
        ("x", "difficulty", "easy"),
        ("", "difficulty", "easy"),
        ("1" * 21, "difficulty", "easy"),
        ("7", "speed", "fast"),
        ("7", "speed", None),
        ("7", "difficulty", "impossible"),
        ("7", "collisions", 1),
        ("7", "mission_time_limit", True),
    ],
)
def test_a_bad_player_option_or_value_raises_and_changes_nothing(
    tmp_path, player, name, value
):
    store = SettingsStore(tmp_path / "s.json")
    with pytest.raises(ValueError):
        store.set_option(player, name, value)
    assert store.options == {} and not store.path.exists()


def test_a_broken_game_options_entry_is_dropped_with_a_warning(tmp_path, caplog):
    path = tmp_path / "s.json"
    path.write_text(
        json.dumps(
            {
                "art_scaling": "engine_fit",
                "game_options": {
                    "7": {"difficulty": "hard", "collisions": 1, "speed": "x"},
                    "bad": {"difficulty": "easy"},
                    "9": "nope",
                },
            }
        )
    )
    with caplog.at_level(logging.WARNING):
        store = SettingsStore(path)
    assert store.get_options("7") == {"difficulty": "hard"}
    assert store.get_options("bad") == {} and store.get_options("9") == {}
    assert store.get() == {"art_scaling": "engine_fit"}
    assert caplog.text.count("WARNING") >= 4


def test_game_options_that_are_not_an_object_are_ignored_with_a_warning(
    tmp_path, caplog
):
    path = tmp_path / "s.json"
    path.write_text('{"game_options": [1]}')
    with caplog.at_level(logging.WARNING):
        assert SettingsStore(path).options == {}
    assert "not an object" in caplog.text


def test_a_broken_file_is_not_overwritten_until_an_option_is_set(tmp_path):
    path = tmp_path / "s.json"
    path.write_text("{broken")
    store = SettingsStore(path)
    assert path.read_text() == "{broken"
    store.set_option("7", "difficulty", "easy")
    assert json.loads(path.read_text())["game_options"] == {"7": {"difficulty": "easy"}}


def test_an_unwritable_file_keeps_the_option_in_memory_and_warns(tmp_path, caplog):
    blocker = tmp_path / "blocker"
    blocker.write_text("a file, not a folder")
    store = SettingsStore(blocker / "s.json")
    with caplog.at_level(logging.WARNING):
        assert store.set_option("7", "difficulty", "easy") is False
    assert store.get_options("7") == {"difficulty": "easy"}
    assert "cannot write" in caplog.text

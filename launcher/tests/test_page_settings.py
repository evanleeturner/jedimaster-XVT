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

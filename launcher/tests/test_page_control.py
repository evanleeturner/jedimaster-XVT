"""The fixed list of commands: every answer and every refusal.

Purpose:
    Prove each of the five commands answers as the list says, that a
    request that is not on the list or not well formed is refused with the
    right code and logged, that a changed setting is saved and announced
    to every page, and that every reply and push satisfies the schema.

Flow:
    A ``Control`` over a made-up install and a temporary settings file;
    ``handle`` called with request text; replies checked against the schema.

Invariants:
    - No game data; no web server (see ``test_page_server.py``).

Call:
    ``pytest tests/test_page_control.py``
"""

from __future__ import annotations

import json
import logging
from pathlib import Path

import pytest
from pagedata import check_valid
from pagedata import LAUNCHER_VERSION
from pagedata import make_install
from pagedata import request

from jedimaster.page.control import Control
from jedimaster.page.control import parse_request
from jedimaster.page.control import Refusal
from jedimaster.page.protocol import MAX_MESSAGE_BYTES
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)


@pytest.fixture
def control(tmp_path: Path) -> Control:
    """Return a control over a made-up install and a fresh settings file."""
    store = SettingsStore(tmp_path / "config" / "settings.json")
    return Control(make_install(tmp_path), store, LAUNCHER_VERSION)


def ask(control: Control, ident: int, command: str, args=None):
    """Run one request and return its reply, checked against the schema."""
    outcome = control.handle(request(ident, command, args))
    return check_valid(outcome.reply)


def test_hello(control):
    reply = ask(control, 5, "hello", {"page_version": "0.1.0"})
    assert reply == {
        "id": 5,
        "ok": True,
        "result": {"launcher_version": LAUNCHER_VERSION, "schema_revision": 1},
    }


def test_install_status_found(control):
    reply = ask(control, 1, "install.status")
    result = reply["result"]
    assert result["found"] is True
    assert result["balance_of_power"] is True
    assert result["path"].endswith("XvT")


def test_install_status_shows_home_as_tilde(tmp_path):
    home = Path.home()
    store = SettingsStore(tmp_path / "s.json")
    control = Control(home / "somewhere" / "XvT", store, LAUNCHER_VERSION)
    assert ask(control, 1, "install.status")["result"]["path"] == "~/somewhere/XvT"
    control = Control(home, store, LAUNCHER_VERSION)
    assert ask(control, 2, "install.status")["result"]["path"] == "~"


def test_install_status_without_balance_of_power(tmp_path):
    root = tmp_path / "plain"
    (root / "Train").mkdir(parents=True)
    control = Control(root, SettingsStore(tmp_path / "s.json"), LAUNCHER_VERSION)
    result = ask(control, 1, "install.status")["result"]
    assert result["found"] is True and result["balance_of_power"] is False


def test_install_not_found(tmp_path):
    control = Control(None, SettingsStore(tmp_path / "s.json"), LAUNCHER_VERSION)
    assert ask(control, 1, "install.status")["result"] == {
        "found": False,
        "path": None,
        "balance_of_power": False,
    }
    menus = ask(control, 2, "missions.list")["result"]["menus"]
    assert len(menus) == 6
    assert all(m["resolved"] is False and m["entries"] == [] for m in menus)
    assert control.status_push()["data"]["install_found"] is False


def test_missions_list(control):
    menus = ask(control, 1, "missions.list")["result"]["menus"]
    by_type = {m["mission_type"]: m for m in menus}
    assert list(by_type) == [
        "training",
        "melee",
        "tournament",
        "combat",
        "battle",
        "campaign",
    ]
    training = by_type["training"]
    assert training["game_path"] == "train\\mission.lst"
    assert training["resolved"] is True
    assert training["entries"] == [
        {
            "section": "Basic",
            "id": 1,
            "available": True,
            "file": "alpha.tie",
            "title": "First Flight",
        },
        {
            "section": "Basic",
            "id": 2,
            "available": False,
            "file": "bravo.tie",
            "title": "Second Flight",
        },
        {
            "section": "Advanced",
            "id": 3,
            "available": True,
            "file": "charlie.tie",
            "title": "Third Flight",
        },
    ]
    assert by_type["melee"]["entries"][0]["title"] == "Open Field"


def test_menu_that_does_not_resolve_is_reported(control):
    by_type = {
        m["mission_type"]: m
        for m in ask(control, 1, "missions.list")["result"]["menus"]
    }
    assert by_type["tournament"]["resolved"] is False
    assert by_type["tournament"]["entries"] == []
    assert by_type["tournament"]["game_path"] == "tourn\\mission.lst"


def test_menu_the_reader_refuses_is_reported_and_logged(control, caplog):
    with caplog.at_level(logging.WARNING):
        menus = ask(control, 1, "missions.list")["result"]["menus"]
    combat = next(m for m in menus if m["mission_type"] == "combat")
    assert combat["resolved"] is False and combat["entries"] == []
    assert "cannot read the menu combat\\mission.lst" in caplog.text


def test_settings_get_defaults(control):
    assert ask(control, 1, "settings.get")["result"] == {
        "settings": {"art_scaling": "whole_pixels"}
    }


def test_settings_set_changes_saves_and_announces(control):
    outcome = control.handle(
        request(3, "settings.set", {"name": "art_scaling", "value": "engine_fit"})
    )
    reply = check_valid(outcome.reply)
    assert reply == {
        "id": 3,
        "ok": True,
        "result": {"settings": {"art_scaling": "engine_fit"}},
    }
    assert [check_valid(p) for p in outcome.pushes] == [
        {
            "event": "settings.changed",
            "data": {"settings": {"art_scaling": "engine_fit"}},
        }
    ]
    assert ask(control, 4, "settings.get")["result"]["settings"] == {
        "art_scaling": "engine_fit"
    }
    saved = json.loads(control.store.path.read_text(encoding="utf-8"))
    assert saved == {"art_scaling": "engine_fit"}
    assert control.handle(request(5, "settings.get")).pushes == []


def test_status_push_is_valid(control):
    push = check_valid(control.status_push())
    assert push == {
        "event": "status",
        "data": {"launcher_version": LAUNCHER_VERSION, "install_found": True},
    }


def error_of(control: Control, text: str, ident: int):
    """Run ``text``, check the error reply, return (code, reply)."""
    reply = check_valid(control.handle(text).reply)
    assert reply["ok"] is False and reply["id"] == ident
    return reply["error"]["code"], reply


@pytest.mark.parametrize(
    ("text", "ident"),
    [
        ("not json", 0),
        ("", 0),
        ("[1, 2]", 0),
        ('"text"', 0),
        ('{"id": 1}', 1),
        ('{"id": 1, "command": "hello"}', 1),
        ('{"id": 1, "command": "hello", "args": {"page_version": "1"}, "x": 1}', 1),
        ('{"id": "1", "command": "hello", "args": {}}', 0),
        ('{"id": 0, "command": "hello", "args": {}}', 0),
        ('{"id": true, "command": "hello", "args": {}}', 0),
        ('{"id": 1.5, "command": "hello", "args": {}}', 0),
        ('{"id": 2147483648, "command": "hello", "args": {}}', 0),
        ('{"id": 1, "command": 7, "args": {}}', 1),
        ('{"id": 1, "command": "hello", "args": []}', 1),
    ],
    ids=[
        "not-json",
        "empty",
        "array",
        "string",
        "id-only",
        "no-args",
        "extra-field",
        "id-text",
        "id-zero",
        "id-bool",
        "id-fraction",
        "id-too-large",
        "command-number",
        "args-list",
    ],
)
def test_bad_message(control, text, ident):
    code, _ = error_of(control, text, ident)
    assert code == "bad_message"


def test_oversize_message_is_a_bad_message(control):
    text = request(1, "hello", {"page_version": "x" * MAX_MESSAGE_BYTES})
    code, _ = error_of(control, text, 0)
    assert code == "bad_message"


def test_the_largest_id_is_accepted(control):
    reply = ask(control, 2**31 - 1, "settings.get")
    assert reply["ok"] is True and reply["id"] == 2**31 - 1


@pytest.mark.parametrize(
    "command", ["", "Hello", "settings", "settings.reset", "install", "shutdown"]
)
def test_command_off_the_list_is_refused_and_logged(control, caplog, command):
    with caplog.at_level(logging.WARNING):
        code, reply = error_of(control, request(9, command), 9)
    assert code == "unknown_command"
    assert "unknown_command" in caplog.text


@pytest.mark.parametrize(
    ("command", "args"),
    [
        ("hello", {}),
        ("hello", {"page_version": ""}),
        ("hello", {"page_version": 3}),
        ("hello", {"page_version": "x" * 65}),
        ("hello", {"page_version": "1", "extra": 1}),
        ("install.status", {"x": 1}),
        ("missions.list", {"x": 1}),
        ("settings.get", {"x": 1}),
        ("settings.set", {}),
        ("settings.set", {"name": "art_scaling"}),
        ("settings.set", {"name": "volume", "value": "high"}),
        ("settings.set", {"name": "art_scaling", "value": "stretched"}),
        ("settings.set", {"name": "art_scaling", "value": 1}),
        ("settings.set", {"name": ["art_scaling"], "value": "engine_fit"}),
        ("settings.set", {"name": "art_scaling", "value": "engine_fit", "x": 1}),
    ],
    ids=[
        "hello-no-version",
        "hello-empty-version",
        "hello-number-version",
        "hello-long-version",
        "hello-extra",
        "status-args",
        "missions-args",
        "get-args",
        "set-empty",
        "set-no-value",
        "set-unknown-name",
        "set-unknown-value",
        "set-number-value",
        "set-list-name",
        "set-extra",
    ],
)
def test_bad_arguments(control, command, args):
    code, _ = error_of(control, request(4, command, args), 4)
    assert code == "bad_arguments"


def test_refused_setting_changes_nothing(control):
    control.handle(request(1, "settings.set", {"name": "art_scaling", "value": "x"}))
    assert control.store.get() == {"art_scaling": "whole_pixels"}
    assert not control.store.path.exists()


def test_parse_request_returns_the_request():
    parsed = parse_request(request(8, "hello", {"page_version": "1"}))
    assert not isinstance(parsed, Refusal)
    assert (parsed.id, parsed.command, parsed.args) == (
        8,
        "hello",
        {"page_version": "1"},
    )


def test_the_size_cap_is_64_kib():
    assert MAX_MESSAGE_BYTES == 65536

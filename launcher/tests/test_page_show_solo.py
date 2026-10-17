"""``page.show_mission`` and ``briefing.get`` by side and file: a solo list entry.

Purpose:
    Prove the second argument shape: an entry of a sided kind's solo list,
    named by side and file, is shown and briefed like a network menu entry,
    including a file the network menu does not list.

Flow:
    Build a made-up install with the solo lists, send each shape, check the
    reply, the push and the bundle.

Invariants:
    - Only a file the solo list of that kind and side offers is shown; the
      match ignores letter case and the push gives the list's own spelling.
    - A side that does not fit the kind, an unsided kind, a file with a path
      in it and a file not in that side's list answer ``shown: false`` or
      are refused.

Call:
    ``pytest tests/test_page_show_solo.py``
"""

from __future__ import annotations

from pathlib import Path

import pytest
from briefingdata import make_install
from briefingdata import mission_bytes
from pagedata import check_valid
from pagedata import LAUNCHER_VERSION
from pagedata import request
from solodata import add_solo_lists

from jedimaster.page.control import Control
from jedimaster.page.control import parse_request
from jedimaster.page.control import Refusal
from jedimaster.page.settings import SettingsStore


@pytest.fixture
def control(tmp_path: Path) -> Control:
    install = add_solo_lists(make_install(tmp_path / "XvT"))
    (install / "Train" / "CHARLIE.TIE").write_bytes(mission_bytes([{"name": "Solo"}]))
    (install / "Train" / "ECHO.TIE").write_bytes(mission_bytes([{"name": "Echo"}]))
    return Control(install, SettingsStore(tmp_path / "s.json"), LAUNCHER_VERSION)


def solo_args(kind: str, side: str, file: str) -> dict:
    return {"mission_type": kind, "side": side, "file": file}


def run(control: Control, command: str, args: dict):
    outcome = control.handle(request(3, command, args))
    for push in outcome.pushes:
        check_valid(push)
    return check_valid(outcome.reply), outcome.pushes


def test_a_solo_entry_is_shown_with_its_side_and_file(control):
    reply, pushes = run(
        control, "page.show_mission", solo_args("training", "rebel", "alpha.tie")
    )
    assert reply["result"] == {"shown": True}
    assert pushes == [
        {
            "event": "page.show_mission",
            "data": {
                "mission_type": "training",
                "side": "rebel",
                "file": "alpha.tie",
                "title": "First Flight",
            },
        }
    ]


def test_a_file_the_network_menu_lacks_is_shown(control):
    _, network = run(
        control, "page.show_mission", {"mission_type": "training", "id": 4}
    )
    assert network == []
    reply, pushes = run(
        control, "page.show_mission", solo_args("training", "rebel", "charlie.tie")
    )
    assert reply["result"] == {"shown": True}
    assert pushes[0]["data"]["title"] == "Third Flight"


def test_the_file_matches_in_any_letter_case_and_is_pushed_as_listed(control):
    reply, pushes = run(
        control, "page.show_mission", solo_args("training", "rebel", "CHARLIE.Tie")
    )
    assert reply["result"] == {"shown": True}
    assert pushes[0]["data"]["file"] == "charlie.tie"


def test_the_network_shape_still_shows_by_id(control):
    reply, pushes = run(
        control, "page.show_mission", {"mission_type": "training", "id": 2}
    )
    assert reply["result"] == {"shown": True}
    assert pushes[0]["data"] == {
        "mission_type": "training",
        "id": 2,
        "title": "Second Flight",
    }


@pytest.mark.parametrize(
    "args",
    [
        solo_args("training", "imperial", "alpha.tie"),
        solo_args("training", "rebel", "echo.tie"),
        solo_args("training", "rebel", "nothing.tie"),
        solo_args("training", "rebel", "bravo.tie"),
        solo_args("training", "rebel", "bad name.tie"),
        solo_args("training", "rebel", "../alpha.tie"),
        solo_args("training", "rebel", "train/alpha.tie"),
        solo_args("training", "rebel", "train\\alpha.tie"),
        solo_args("combat", "rebel", "alpha.tie"),
    ],
)
def test_a_file_not_in_that_sides_list_shows_nothing(control, args):
    for command in ("page.show_mission", "briefing.get"):
        reply, pushes = run(control, command, args)
        assert pushes == []
        assert reply["ok"] is True
        result = reply["result"]
        assert result == (
            {"shown": False}
            if command == "page.show_mission"
            else {"found": False, "bundle": None}
        )


@pytest.mark.parametrize(
    "args",
    [
        solo_args("melee", "none", "alpha.tie"),
        solo_args("melee", "rebel", "alpha.tie"),
        solo_args("tournament", "none", "first.tou"),
        solo_args("training", "none", "alpha.tie"),
        solo_args("training", "sith", "alpha.tie"),
        solo_args("training", "rebel", ""),
        solo_args("training", "rebel", "a" * 65),
        {"mission_type": "training", "side": "rebel", "file": 4},
        {"mission_type": "training", "side": "rebel"},
        {"mission_type": "training", "file": "alpha.tie"},
        {"mission_type": "training", "id": 1, "file": "alpha.tie"},
        {"mission_type": "training", "id": 1, "side": "rebel", "file": "alpha.tie"},
        {"mission_type": "training", "side": "rebel", "file": "alpha.tie", "id": 1},
    ],
)
def test_bad_solo_arguments_are_refused_by_both_commands(control, args):
    for command in ("page.show_mission", "briefing.get"):
        refusal = parse_request(request(1, command, args))
        assert isinstance(refusal, Refusal) and refusal.code == "bad_arguments"


def test_a_solo_entry_is_briefed(control):
    reply, pushes = run(
        control, "briefing.get", solo_args("training", "rebel", "CHARLIE.TIE")
    )
    assert pushes == []
    assert reply["result"]["found"] is True
    assert reply["result"]["bundle"]["format"] == "jedimaster.briefing"


def test_the_other_sides_entry_is_briefed_from_its_own_list(control):
    reply, _ = run(
        control, "briefing.get", solo_args("training", "imperial", "echo.tie")
    )
    assert reply["result"]["found"] is True


def test_a_listed_file_that_is_missing_is_not_found(control, tmp_path):
    (tmp_path / "XvT" / "Train" / "CHARLIE.TIE").unlink()
    reply, _ = run(
        control, "briefing.get", solo_args("training", "rebel", "charlie.tie")
    )
    assert reply["result"] == {"found": False, "bundle": None}
    shown, pushes = run(
        control, "page.show_mission", solo_args("training", "rebel", "charlie.tie")
    )
    assert shown["result"] == {"shown": True} and len(pushes) == 1


def test_a_new_page_gets_the_last_solo_mission_shown(control):
    run(control, "page.show_mission", solo_args("training", "rebel", "charlie.tie"))
    pushes = control.connect_pushes()
    shown = [p for p in pushes if p["event"] == "page.show_mission"]
    assert shown[0]["data"]["file"] == "charlie.tie"


def test_no_install_shows_and_briefs_nothing(tmp_path):
    control = Control(None, SettingsStore(tmp_path / "s.json"), LAUNCHER_VERSION)
    args = solo_args("training", "rebel", "alpha.tie")
    assert run(control, "page.show_mission", args)[0]["result"] == {"shown": False}
    assert run(control, "briefing.get", args)[0]["result"] == {
        "found": False,
        "bundle": None,
    }

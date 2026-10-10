"""``briefing.get``: found, not found, refused, and the export command.

Purpose:
    Prove the seventh command answers with the mission's bundle when the
    mission is in the type's network menu and its file reads, with
    ``found: false`` in every other case, refuses bad arguments like
    ``page.show_mission`` does, picks Balance of Power's file first, makes no
    push, and that every reply satisfies the control schema; and that
    ``briefing export`` writes the same bundle to a file.

Flow:
    A ``Control`` over the made-up install of ``briefingdata``; replies
    checked with ``pagedata.check_valid``; ``main`` for the command line.

Invariants:
    - No game data; the page's server is not involved.

Call:
    ``pytest tests/test_briefing_control.py``
"""

from __future__ import annotations

import json
import logging
from pathlib import Path

import pytest
from briefingdata import briefing_points
from briefingdata import flagged
from briefingdata import make_install
from briefingdata import mission_bytes
from briefingdata import point
from pagedata import check_valid
from pagedata import LAUNCHER_VERSION
from pagedata import request

from jedimaster.__main__ import main
from jedimaster.page.control import Control
from jedimaster.page.control import parse_request
from jedimaster.page.control import Refusal
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)


@pytest.fixture
def install(tmp_path: Path) -> Path:
    return make_install(tmp_path / "XvT")


@pytest.fixture
def control(tmp_path: Path, install: Path) -> Control:
    return Control(install, SettingsStore(tmp_path / "s.json"), LAUNCHER_VERSION)


def get(control: Control, mission_type: str, ident: int):
    outcome = control.handle(
        request(4, "briefing.get", {"mission_type": mission_type, "id": ident})
    )
    assert outcome.pushes == []
    return check_valid(outcome.reply)


def test_a_found_mission_answers_with_its_bundle(control):
    reply = get(control, "training", 1)
    assert reply["ok"] is True and reply["result"]["found"] is True
    bundle = reply["result"]["bundle"]
    assert bundle["format"] == "jedimaster.briefing" and len(bundle["teams"]) == 10


def test_balance_of_powers_file_is_read_first(control):
    groups = get(control, "training", 1)["result"]["bundle"]["groups"]
    assert [g["name"] for g in groups] == ["Bop"]


def test_the_base_file_is_read_when_balance_of_power_has_none(control, install):
    (install / "BalanceOfPower" / "TRAIN" / "ALPHA.TIE").unlink()
    groups = get(control, "training", 1)["result"]["bundle"]["groups"]
    assert [g["name"] for g in groups] == ["Base"]


@pytest.mark.parametrize(
    ("mission_type", "ident"),
    [("training", 2), ("training", 3), ("training", 99), ("melee", 1), ("battle", 0)],
)
def test_every_other_mission_is_not_found(control, mission_type, ident):
    # id 2 names a file that is not there, id 3 a file that is not there, 99
    # no entry, melee and battle menus that do not resolve.
    result = get(control, mission_type, ident)["result"]
    assert result == {"found": False, "bundle": None}


def test_a_file_that_is_not_a_mission_is_not_found(control, install, caplog):
    (install / "BalanceOfPower" / "TRAIN" / "ALPHA.TIE").write_bytes(b"not a mission")
    (install / "Train" / "ALPHA.TIE").write_bytes(b"not a mission")
    with caplog.at_level(logging.WARNING):
        result = get(control, "training", 1)["result"]
    assert result == {"found": False, "bundle": None}
    assert "cannot build the briefing" in caplog.text


def test_a_missing_font_is_not_found(control, install, caplog):
    (install / "TIMES10.ABP").unlink()
    with caplog.at_level(logging.WARNING):
        result = get(control, "training", 1)["result"]
    assert result["found"] is False


def test_no_install_is_not_found(tmp_path):
    control = Control(None, SettingsStore(tmp_path / "s.json"), LAUNCHER_VERSION)
    assert get(control, "training", 1)["result"] == {"found": False, "bundle": None}


@pytest.mark.parametrize(
    "args",
    [
        {},
        {"mission_type": "training"},
        {"id": 1},
        {"mission_type": "practice", "id": 1},
        {"mission_type": "training", "id": -1},
        {"mission_type": "training", "id": 1.5},
        {"mission_type": "training", "id": 2**31},
        {"mission_type": "training", "id": 1, "team": 0},
    ],
)
def test_bad_arguments_are_refused(control, args):
    text = request(8, "briefing.get", args)
    refusal = parse_request(text)
    assert isinstance(refusal, Refusal) and refusal.code == "bad_arguments"
    reply = check_valid(control.handle(text).reply)
    assert reply["ok"] is False and reply["error"]["code"] == "bad_arguments"


def test_the_two_commands_check_the_same_arguments(control):
    for args in ({"mission_type": "campaign", "id": 0}, {"mission_type": "x", "id": 0}):
        shown = parse_request(request(1, "page.show_mission", args))
        briefed = parse_request(request(1, "briefing.get", args))
        assert isinstance(shown, Refusal) == isinstance(briefed, Refusal)


def test_the_bundle_of_a_chosen_mission_has_what_the_file_says(control, install):
    brief = {"viewed_by_team": flagged(1), "running_time": 90}
    points = briefing_points(point(3, 4))
    data = mission_bytes([{"name": "Zed", "waypoints": points}], [brief], ["A", "B"])
    (install / "BalanceOfPower" / "TRAIN" / "ALPHA.TIE").write_bytes(data)
    bundle = get(control, "training", 1)["result"]["bundle"]
    assert (
        bundle["teams"][1]["briefing"] == 0
        and bundle["briefings"][0]["running_time"] == 90
    )
    assert bundle["groups"][0]["points"][0] == {"x": 3, "y": 4, "enabled": True}


def test_export_writes_the_bundle_of_one_file(tmp_path, install, capsys):
    out = tmp_path / "bundle.json"
    source = install / "Train" / "ALPHA.TIE"
    code = main(["briefing", "export", "--install", str(install)]
                + ["--file", str(source), "--out", str(out)])  # fmt: skip
    assert code == 0 and "bundle.json" in capsys.readouterr().out
    bundle = json.loads(out.read_text(encoding="utf-8"))
    assert [g["name"] for g in bundle["groups"]] == ["Base"]


def test_export_needs_an_install_and_a_file(tmp_path, install, caplog):
    out = tmp_path / "bundle.json"
    base = ["briefing", "export", "--out", str(out)]
    assert main([*base, "--install", str(tmp_path / "none"), "--file", "x"]) == 2
    assert main([*base, "--install", str(install), "--file", str(tmp_path / "x")]) == 2
    assert not out.exists()
    with pytest.raises(SystemExit):
        main(["briefing", "export", "--install", str(install)])


def test_export_exits_1_for_a_file_that_is_not_a_mission(tmp_path, install):
    broken = tmp_path / "broken.tie"
    broken.write_bytes(b"nope")
    args = ["--install", str(install), "--file", str(broken)]
    assert main(["briefing", "export", *args, "--out", str(tmp_path / "o.json")]) == 1

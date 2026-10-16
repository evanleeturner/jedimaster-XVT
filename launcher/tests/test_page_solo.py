"""The solo lists: what a pilot of one kind and side may be offered.

Purpose:
    Prove which entries a solo list offers (never ``&``, ``*`` like any
    other, one per file, only launchable names), which list is
    read for each kind and side, and that a side that does not fit is
    refused.

Flow:
    ``solo_menu`` over the made-up solo install; ``side_problem`` directly.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_page_solo.py``
"""

from __future__ import annotations

import logging

import pytest
from solodata import make_solo_install

from jedimaster.page.solo import find
from jedimaster.page.solo import side_problem
from jedimaster.page.solo import solo_menu

logger = logging.getLogger(__name__)


@pytest.fixture
def install(tmp_path):
    return make_solo_install(tmp_path)


def test_a_sided_kind_reads_its_side_s_list(install):
    rebel = solo_menu(install, "training", "rebel")
    imperial = solo_menu(install, "training", "imperial")
    assert rebel["game_path"] == "train\\rebel.lst"
    assert imperial["game_path"] == "train\\imperial.lst"
    assert [e["file"] for e in imperial["entries"]] == ["echo.tie"]


def test_an_unsided_kind_reads_its_mission_list(install):
    menu = solo_menu(install, "melee", "none")
    assert menu["game_path"] == "melee\\mission.lst"
    assert [e["file"] for e in menu["entries"]] == ["delta.tie"]


def test_a_mission_marked_ampersand_is_never_offered(install):
    files = [e["file"] for e in solo_menu(install, "training", "rebel")["entries"]]
    assert "bravo.tie" not in files


def test_a_mission_marked_star_is_offered_like_any_other(install):
    entries = {e["file"]: e for e in solo_menu(install, "training", "rebel")["entries"]}
    assert entries["charlie.tie"] == {
        "section": "Advanced",
        "file": "charlie.tie",
        "title": "Third Flight",
    }
    assert set(entries["alpha.tie"]) == {"section", "file", "title"}


def test_a_file_name_the_launch_cannot_carry_is_not_offered(install):
    files = [e["file"] for e in solo_menu(install, "training", "rebel")["entries"]]
    assert "bad name.tie" not in files


def test_sections_and_titles_come_with_the_entries(install):
    first, *_ = solo_menu(install, "training", "rebel")["entries"]
    assert (first["section"], first["title"]) == ("Basic", "First Flight")


def test_a_sequence_is_offered_as_its_file(install):
    assert [e["file"] for e in solo_menu(install, "tournament", "none")["entries"]] == [
        "first.tou"
    ]
    assert [e["file"] for e in solo_menu(install, "campaign", "rebel")["entries"]] == [
        "camp.cmp"
    ]


def test_a_missing_list_or_install_does_not_resolve(install):
    assert solo_menu(install, "combat", "rebel")["resolved"] is False
    assert solo_menu(None, "melee", "none") == {
        "mission_type": "melee",
        "side": "none",
        "game_path": "melee\\mission.lst",
        "resolved": False,
        "entries": [],
    }


def test_a_repeated_file_is_offered_once(tmp_path):
    from listdata import crlf

    install = make_solo_install(tmp_path)
    (install / "Melee/MISSION.LST").write_bytes(
        crlf("1", "A.TIE", "One", "2", "a.tie", "Two")
    )
    entries = solo_menu(install, "melee", "none")["entries"]
    assert [(e["file"], e["title"]) for e in entries] == [("a.tie", "One")]


@pytest.mark.parametrize(
    "kind, side, fits",
    [
        ("training", "rebel", True),
        ("training", "none", False),
        ("combat", "imperial", True),
        ("campaign", "none", False),
        ("melee", "none", True),
        ("melee", "rebel", False),
        ("battle", "imperial", False),
    ],
)
def test_a_side_fits_a_kind_only_when_the_kind_takes_one(kind, side, fits):
    assert (side_problem(kind, side) is None) is fits


def test_reading_a_list_with_a_side_that_does_not_fit_raises(install):
    with pytest.raises(ValueError):
        solo_menu(install, "melee", "rebel")


def test_find_matches_a_file_in_any_letter_case(install):
    menu = solo_menu(install, "training", "rebel")
    assert find(menu, "ALPHA.tie")["title"] == "First Flight"
    assert find(menu, "bravo.tie") is None

"""The launcher's pilots: a Discord name fitted to the game's rule, and the pilot files.

Purpose:
    Prove ``fit_name`` makes any text into a name ``name_ok`` accepts or
    gives up, ``first_name`` falls back from the display name to the
    account name to ``Pilot`` and four digits, and ``PilotStore`` writes one
    file per account in the stated shape, renames a pilot once only, and
    never writes over a file it cannot read.

Flow:
    Each test works in a temporary folder; files are read back as JSON.

Invariants:
    - No Discord; no game data.

Call:
    ``pytest tests/test_pilot_store.py``
"""

from __future__ import annotations

import json
import logging

import pytest

from jedimaster.launch.pilot_store import first_name
from jedimaster.launch.pilot_store import Pilot
from jedimaster.launch.pilot_store import pilot_folder
from jedimaster.launch.pilot_store import PilotFileError
from jedimaster.launch.pilot_store import PilotStore
from jedimaster.launch.pilots import fit_name
from jedimaster.launch.pilots import name_ok

logger = logging.getLogger(__name__)


# ---- fitting a name -------------------------------------------------------


@pytest.mark.parametrize(
    ("text", "fitted"),
    [
        ("Mr. Webbin", "Mr. Webbin"),
        ("mr.webbin", "mr.webbin"),
        ("José Álvarez", "Jose Alvarez"),
        ("Supercalifragilistic", "Supercalifra"),
        ("Darth Vader 2nd", "Darth Vader"),
        ("  --Ace--  ", "Ace"),
        ("A    B", "A B"),
        ("Red  Five", "Red Five"),
        ("🔥 Blaze 🔥", "Blaze"),
        ("SFX Ace", "Ace"),
        ("Han@Solo", "HanSolo"),
        ("C-3PO #1", "C-3PO 1"),
        ("Musicman", "man"),
        ("musfxic", "muic"),
        ("vosfxice", ""),
        ("🔥🔥", ""),
        ("", ""),
    ],
)
def test_a_name_is_fitted_to_the_rule(text, fitted):
    assert fit_name(text) == fitted


def test_a_switch_word_made_by_taking_one_out_is_taken_out_too():
    # "s" + "voice" + "fx": taking "voice" out leaves "sfx", a switch word
    # checked before "voice", so one pass over the words is not enough.
    assert fit_name("Ace svoicefx") == "Ace"


def test_a_name_cut_at_twelve_ends_on_a_letter_or_digit():
    assert fit_name("Wedge  Antilles") == "Wedge Antill"
    assert fit_name("Biggs Darkl. ighter") == "Biggs Darkl"


@pytest.mark.parametrize(
    "text",
    [
        "Mr. Webbin",
        "🔥",
        "x" * 40,
        "a.b.c",
        "Tie Fighter!!",
        "Ünïcödé",
        "Biggs Darkl. ighter",
        "Ace svoicefx",
    ],
)
def test_every_fitted_name_passes_the_rule_or_is_empty(text):
    fitted = fit_name(text)
    assert fitted == "" or name_ok(fitted)


def test_the_first_name_is_the_display_name_fitted():
    assert first_name("Mr. Webbin", "mr.webbin", 328912384850919444) == "Mr. Webbin"


def test_the_first_name_falls_back_to_the_account_name():
    assert first_name("🔥🔥", "mr.webbin", 328912384850919444) == "mr.webbin"
    assert first_name(None, "mr.webbin", 328912384850919444) == "mr.webbin"


def test_the_first_name_falls_back_to_pilot_and_four_digits():
    assert first_name("🔥", "__", 328912384850919444) == "Pilot 9444"
    assert first_name("", "", 7) == "Pilot 7"


# ---- the store ------------------------------------------------------------


def test_the_folder_sits_beside_the_settings_file(tmp_path):
    assert pilot_folder(tmp_path / "settings.json") == tmp_path / "pilots"


def test_no_file_is_no_pilot(tmp_path):
    assert PilotStore(tmp_path / "pilots").get(7001) is None


def test_a_made_pilot_is_one_file_in_the_stated_shape(tmp_path):
    store = PilotStore(tmp_path / "pilots")
    assert store.make(7001, "Mr. Webbin") == Pilot(7001, "Mr. Webbin", False)
    path = tmp_path / "pilots" / "7001.json"
    assert json.loads(path.read_text(encoding="utf-8")) == {
        "format": 1,
        "account": "7001",
        "name": "Mr. Webbin",
        "renamed": False,
    }
    assert [p.name for p in (tmp_path / "pilots").iterdir()] == ["7001.json"]
    assert store.get(7001) == Pilot(7001, "Mr. Webbin", False)


def test_a_pilot_is_not_made_twice(tmp_path):
    store = PilotStore(tmp_path / "pilots")
    store.make(7001, "Mr. Webbin")
    with pytest.raises(PilotFileError):
        store.make(7001, "Other")
    assert store.get(7001).name == "Mr. Webbin"


def test_a_name_off_the_rule_is_not_made(tmp_path):
    store = PilotStore(tmp_path / "pilots")
    with pytest.raises(ValueError):
        store.make(7001, "Music Man")
    assert store.get(7001) is None


def test_a_pilot_is_renamed_once(tmp_path):
    store = PilotStore(tmp_path / "pilots")
    store.make(7001, "Mr. Webbin")
    assert store.rename(7001, "Red Five") == Pilot(7001, "Red Five", True)
    assert store.get(7001) == Pilot(7001, "Red Five", True)
    with pytest.raises(PilotFileError):
        store.rename(7001, "Red Six")
    assert store.get(7001).name == "Red Five"


def test_a_rename_off_the_rule_changes_nothing(tmp_path):
    store = PilotStore(tmp_path / "pilots")
    store.make(7001, "Mr. Webbin")
    with pytest.raises(ValueError):
        store.rename(7001, "sfx")
    assert store.get(7001) == Pilot(7001, "Mr. Webbin", False)


def test_a_rename_without_a_pilot_is_refused(tmp_path):
    with pytest.raises(PilotFileError):
        PilotStore(tmp_path / "pilots").rename(7001, "Red Five")


@pytest.mark.parametrize(
    "text",
    [
        "not json",
        "[]",
        '{"format": 1, "account": "7001", "name": "Ace"}',
        '{"format": 2, "account": "7001", "name": "Ace", "renamed": false}',
        '{"format": true, "account": "7001", "name": "Ace", "renamed": false}',
        '{"format": 1, "account": "7002", "name": "Ace", "renamed": false}',
        '{"format": 1, "account": "7001", "name": "sfx", "renamed": false}',
        '{"format": 1, "account": "7001", "name": "Ace", "renamed": 0}',
        '{"format": 1, "account": "7001", "name": "Ace", "renamed": false, "x": 1}',
    ],
)
def test_a_file_that_is_not_this_account_s_pilot_is_refused(tmp_path, text):
    folder = tmp_path / "pilots"
    folder.mkdir()
    (folder / "7001.json").write_text(text, encoding="utf-8")
    with pytest.raises(PilotFileError):
        PilotStore(folder).get(7001)


def test_a_bad_file_is_never_written_over(tmp_path):
    folder = tmp_path / "pilots"
    folder.mkdir()
    (folder / "7001.json").write_text("not json", encoding="utf-8")
    store = PilotStore(folder)
    with pytest.raises(PilotFileError):
        store.make(7001, "Mr. Webbin")
    with pytest.raises(PilotFileError):
        store.rename(7001, "Mr. Webbin")
    assert (folder / "7001.json").read_text(encoding="utf-8") == "not json"


def test_no_temporary_file_is_left(tmp_path):
    store = PilotStore(tmp_path / "pilots")
    store.make(7001, "Mr. Webbin")
    store.rename(7001, "Red Five")
    assert [p.name for p in (tmp_path / "pilots").iterdir()] == ["7001.json"]

"""The game's pilot folder and the pilots in it.

Purpose:
    Prove the default folder for each system, the rules a pilot's name must
    pass to be offered, and that listing reads names the way the engine does.

Flow:
    ``default_pilot_folder`` with made-up environments; ``name_ok`` on good
    and bad names; ``list_pilots`` over temporary folders.

Invariants:
    - No game data: files hold a name and filler.

Call:
    ``pytest tests/test_launch_pilots.py``
"""

from __future__ import annotations

import logging
from pathlib import Path

import pytest
from solodata import make_pilots

from jedimaster.launch.pilots import default_pilot_folder
from jedimaster.launch.pilots import list_pilots
from jedimaster.launch.pilots import name_ok

logger = logging.getLogger(__name__)


def test_linux_uses_xdg_data_home_else_the_local_share_folder():
    home = Path.home()
    assert default_pilot_folder("linux", {"XDG_DATA_HOME": "/data"}) == Path(
        "/data/TotallyOpen/OpenXvT"
    )
    assert default_pilot_folder("linux", {}) == (
        home / ".local/share/TotallyOpen/OpenXvT"
    )


def test_windows_uses_appdata_else_the_roaming_folder():
    assert default_pilot_folder("win32", {"APPDATA": "C:/Users/x/AppData/Roaming"}) == (
        Path("C:/Users/x/AppData/Roaming/TotallyOpen/OpenXvT")
    )
    assert default_pilot_folder("win32", {}) == (
        Path.home() / "AppData/Roaming/TotallyOpen/OpenXvT"
    )


def test_macos_uses_application_support():
    assert default_pilot_folder("darwin", {}) == (
        Path.home() / "Library/Application Support/TotallyOpen/OpenXvT"
    )


def test_another_system_has_no_default_folder():
    assert default_pilot_folder("freebsd14", {}) is None


@pytest.mark.parametrize("name", ["Ace", "Bravo Two", "a.b-c_d", "x" * 12, "7"])
def test_a_good_name_passes(name):
    assert name_ok(name)


@pytest.mark.parametrize(
    "name",
    [
        "",
        "x" * 13,
        " Ace",
        "Ace ",
        "-Ace",
        "Ace.",
        "A~B",
        "A\tB",
        "Aé",
        "MyMusic1",
        "SFX",
    ],
)
def test_a_bad_name_fails(name):
    # the last two hold flight switch words the engine would read as switches
    assert not name_ok(name)


def test_the_listing_reads_each_plt_file_s_name_in_file_name_order(tmp_path):
    folder = make_pilots(tmp_path, ("Zed", "Ace", "Mid"))
    assert list_pilots(folder) == ["Zed", "Ace", "Mid"]  # pilot0, pilot1, pilot2


def test_the_name_ends_at_the_first_zero_byte(tmp_path):
    folder = tmp_path / "p"
    folder.mkdir()
    (folder / "a.plt").write_bytes(b"Ace\0junk\0junk" + b"zzzz")
    assert list_pilots(folder) == ["Ace"]


def test_only_plt_files_count_in_any_letter_case(tmp_path):
    folder = make_pilots(tmp_path, ("Ace",))
    (folder / "other.pl2").write_bytes(b"Nope".ljust(20, b"\0"))
    (folder / "UP.PLT").write_bytes(b"Upper".ljust(20, b"\0"))
    (folder / "sub.plt").mkdir()
    assert list_pilots(folder) == ["Ace", "Upper"]
    assert "Nope" not in list_pilots(folder)


def test_a_bad_or_repeated_name_is_skipped(tmp_path):
    folder = tmp_path / "p"
    folder.mkdir()
    (folder / "a.plt").write_bytes(b"Ace".ljust(20, b"\0"))
    (folder / "b.plt").write_bytes(b"ACE".ljust(20, b"\0"))
    (folder / "c.plt").write_bytes(b"bad~name".ljust(20, b"\0"))
    assert list_pilots(folder) == ["Ace"]


def test_a_missing_folder_or_no_folder_is_none(tmp_path):
    assert list_pilots(tmp_path / "nowhere") is None
    assert list_pilots(None) is None
    empty = tmp_path / "empty"
    empty.mkdir()
    assert list_pilots(empty) == []

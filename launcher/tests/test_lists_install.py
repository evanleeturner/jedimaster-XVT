"""The install side of the lists: kinds, menu files, paths, bitmap headers.

Purpose:
    Prove ``list_files`` finds each kind in the install and in
    ``BalanceOfPower/``, ``menu_paths`` follows the notes' table,
    ``kind_of`` tells kinds by name, a game path resolves with or without
    Balance of Power, the sheets' file label, and the BMP header reading.

Flow:
    Build a fake install of synthetic files under ``tmp_path``.

Invariants:
    - No game data; the files hold synthetic lists or BMP headers.

Call:
    ``pytest tests/test_lists_install.py``
"""

from __future__ import annotations

import logging
from pathlib import Path

import pytest
from listdata import bmp_header
from listdata import crlf

from jedimaster.lists import files
from jedimaster.lists import read_images
from jedimaster.lists.text import ListFormatError

logger = logging.getLogger(__name__)


@pytest.fixture
def fake_install(tmp_path: Path) -> Path:
    """Return a fake install with lists and bitmaps in both trees."""
    root = tmp_path / "XvT"
    content = {
        "Train/MISSION.LST": crlf("1", "a.tie", "A"),
        "Train/notes.txt": b"",
        "Tourn/MISSION.LST": crlf("0", "t0.lst", "T"),
        "Tourn/T0.LST": crlf("0"),
        "Melee/MISSION.LST": b"",
        "frontres/ICONS.LST": crlf(
            "//", "frontres\\a.bmp one 1", "frontres\\b.bmp two 1"
        ),
        "frontres/A.BMP": bmp_header(9, -7),
        "frontres/B.BMP": b"not a bitmap at all, long enough",
        "frontres/FRNTSPEC.LST": crlf("a.opt 1"),
        "Sfx/SFX.LST": crlf("//"),
        "BalanceOfPower/CAMPAIGN/rebel.lst": crlf("1", "c1.lst", "C"),
        "BalanceOfPower/CAMPAIGN/campgn1.lst": crlf("0"),
        "BalanceOfPower/TRAIN/mission.lst": crlf("2", "b.tie", "B"),
        "BalanceOfPower/FRONTRES/icons.lst": crlf("//"),
        "BalanceOfPower/FRONTRES/a.bmp": bmp_header(4, 5, core=True),
        "BalanceOfPower/MOVIES/cutscene.lst": crlf("0"),
        "BalanceOfPower/FRONTRES/campawds.lst": crlf("0"),
    }
    for rel, data in content.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return root


def rels(root: Path, paths: list[Path]) -> list[str]:
    return [p.relative_to(root).as_posix() for p in paths]


def test_list_files_by_kind(fake_install):
    found = files.list_files(fake_install)
    assert list(found) == list(files.KINDS)
    assert rels(fake_install, found["menu"]) == [
        "Train/MISSION.LST",
        "Melee/MISSION.LST",
        "Tourn/MISSION.LST",
        "BalanceOfPower/TRAIN/mission.lst",
        "BalanceOfPower/CAMPAIGN/rebel.lst",
    ]
    assert rels(fake_install, found["sequence"]) == [
        "Tourn/T0.LST",
        "BalanceOfPower/CAMPAIGN/campgn1.lst",
    ]
    assert rels(fake_install, found["images"]) == [
        "frontres/ICONS.LST",
        "BalanceOfPower/FRONTRES/icons.lst",
    ]
    assert rels(fake_install, found["ships"]) == ["frontres/FRNTSPEC.LST"]
    assert rels(fake_install, found["sounds"]) == ["Sfx/SFX.LST"]
    assert rels(fake_install, found["cutscenes"]) == [
        "BalanceOfPower/MOVIES/cutscene.lst"
    ]
    assert rels(fake_install, found["awards"]) == [
        "BalanceOfPower/FRONTRES/campawds.lst"
    ]


def test_menu_paths_table():
    paths = files.menu_paths()
    assert list(paths) == [
        "training",
        "melee",
        "tournament",
        "combat",
        "battle",
        "campaign",
    ]
    assert paths["combat"] == {
        "network": "combat\\mission.lst",
        "rebel": "combat\\rebel.lst",
        "imperial": "combat\\imperial.lst",
    }
    assert set(paths["battle"].values()) == {"battle\\mission.lst"}


@pytest.mark.parametrize(
    ("path", "kind"),
    [
        ("x/TRAIN/REBEL.LST", "menu"),
        ("x/Tourn/t9.lst", "sequence"),
        ("x/CAMPAIGN/campgn3.lst", "sequence"),
        ("x/Train/other.lst", None),
        ("x/frontres/MAPICONS.LST", "images"),
        ("frntspec.lst", "ships"),
        ("Sfx/sfx.lst", "sounds"),
        ("cutscene.lst", "cutscenes"),
        ("campawds.lst", "awards"),
        ("x/Battle/readme.txt", None),
    ],
)
def test_kind_of(path, kind):
    assert files.kind_of(path) == kind


def test_resolve_with_and_without_balance_of_power(fake_install):
    with_bop = files.resolve(fake_install, "train\\mission.lst")
    assert with_bop == fake_install / "BalanceOfPower/TRAIN/mission.lst"
    base = files.resolve(fake_install, "train\\mission.lst", balance_of_power=False)
    assert base == fake_install / "Train/MISSION.LST"
    assert (
        files.resolve(fake_install, "campaign\\rebel.lst", balance_of_power=False)
        is None
    )
    assert files.sheet_file(fake_install, "TRAIN\\Mission.lst", with_bop) == (
        "BalanceOfPower/train/mission.lst"
    )
    assert (
        files.sheet_file(fake_install, "train\\mission.lst", base)
        == "train/mission.lst"
    )


def test_bmp_header_sizes(fake_install, tmp_path):
    assert files.read_bmp_size(fake_install / "frontres/A.BMP") == (9, 7)
    assert files.read_bmp_size(fake_install / "BalanceOfPower/FRONTRES/a.bmp") == (4, 5)
    with pytest.raises(ListFormatError):
        files.read_bmp_size(fake_install / "frontres/B.BMP")
    short = tmp_path / "short.bmp"
    short.write_bytes(b"BM")
    with pytest.raises(ListFormatError):
        files.read_bmp_size(short)


def test_install_bitmaps_skip_missing_and_unreadable(fake_install):
    images = read_images((fake_install / "frontres/ICONS.LST").read_bytes())
    base = files.install_bitmaps(fake_install, images, balance_of_power=False)
    assert {k: (b.width, b.height) for k, b in base.items()} == {
        "frontres\\a.bmp": (9, 7)
    }
    bop = files.install_bitmaps(fake_install, images)
    assert {k: (b.width, b.height) for k, b in bop.items()} == {
        "frontres\\a.bmp": (4, 5)
    }
    assert all(b.compresses for b in bop.values())
    odd = read_images(crlf("//", "..\\x.bmp one 1", "nowhere.bmp two 1"))
    assert files.install_bitmaps(fake_install, odd) == {}

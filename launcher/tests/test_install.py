"""Install finding, the game-path rule, and mission listing.

Purpose:
    Prove the path rule in a temporary folder: backslashes become slashes;
    each part matches in any letter case; ``BalanceOfPower/`` is searched
    before the install folder; paths may not leave the install. Also the
    install test, the candidate folders and the listing by folder.

Flow:
    Build a fake install tree under ``tmp_path`` with empty files.

Invariants:
    - No game data; files are empty placeholders.

Call:
    ``pytest tests/test_install.py``
"""

from __future__ import annotations

import logging
from pathlib import Path

import pytest

from jedimaster import install as inst

logger = logging.getLogger(__name__)


@pytest.fixture
def fake_install(tmp_path: Path) -> Path:
    """Return a fake install tree of empty placeholder files."""
    root = tmp_path / "XvT"
    for rel in (
        "Train/1TA01BF.TIE",
        "Train/ONLYXVT.TIE",
        "Train/notes.txt",
        "Combat/8B01G01.TIE",
        "Melee/8MA1GW09.TIE",
        "Battle/B1.TIE",
        "BalanceOfPower/TRAIN/1ta01bf.tie",
        "BalanceOfPower/MELEE/8mf1wa.tie",
        "BalanceOfPower/TRAIN/sub/deep.tie",
    ):
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"")
    return root


def test_backslashes_are_separators():
    assert inst.game_path_parts("TRAIN\\1TA01BF.TIE") == ["TRAIN", "1TA01BF.TIE"]
    assert inst.game_path_parts("a\\\\b/./c/") == ["a", "b", "c"]
    assert inst.game_path_parts("") == []


@pytest.mark.parametrize(
    "bad", ["..\\x.tie", "TRAIN/../../x", "\\TRAIN\\x", "/etc/x", "C:\\x.tie"]
)
def test_paths_leaving_the_install_are_refused(bad):
    with pytest.raises(ValueError):
        inst.game_path_parts(bad)


def test_balance_of_power_is_searched_first(fake_install):
    found = inst.resolve_game_path(fake_install, "TRAIN\\1TA01BF.TIE")
    assert found == fake_install / "BalanceOfPower/TRAIN/1ta01bf.tie"


def test_install_folder_is_the_fallback(fake_install):
    found = inst.resolve_game_path(fake_install, "train\\onlyxvt.tie")
    assert found == fake_install / "Train/ONLYXVT.TIE"
    found = inst.resolve_game_path(fake_install, "COMBAT/8b01g01.tie")
    assert found == fake_install / "Combat/8B01G01.TIE"


def test_every_part_matches_in_any_case(fake_install):
    found = inst.resolve_game_path(fake_install, "bAlAnCeOfPoWeR\\train\\SUB\\DEEP.TIE")
    assert found == fake_install / "BalanceOfPower/TRAIN/sub/deep.tie"
    assert (
        inst.resolve_game_path(fake_install, "melee")
        == fake_install / "BalanceOfPower/MELEE"
    )


def test_missing_paths_resolve_to_none(fake_install):
    assert inst.resolve_game_path(fake_install, "TRAIN\\nothere.tie") is None
    assert inst.resolve_game_path(fake_install, "") is None


def test_exact_case_wins_over_other_case(tmp_path):
    root = tmp_path / "XvT"
    (root / "Train").mkdir(parents=True)
    (root / "Train" / "a.tie").write_bytes(b"1")
    (root / "Train" / "A.tie").write_bytes(b"2")
    assert inst.resolve_game_path(root, "Train/A.tie") == root / "Train/A.tie"
    assert inst.resolve_game_path(root, "Train/a.tie") == root / "Train/a.tie"
    # Neither spelling exact: the first in sorted order ("A.tie") is taken.
    assert inst.resolve_game_path(root, "TRAIN/a.TIE") == root / "Train/A.tie"


def test_is_install_and_find_install(fake_install, tmp_path, monkeypatch):
    assert inst.is_install(fake_install)
    assert not inst.is_install(tmp_path / "missing")
    assert not inst.is_install(tmp_path)
    assert inst.find_install(fake_install) == fake_install
    assert inst.find_install(tmp_path) is None
    monkeypatch.setattr(
        inst, "candidate_folders", lambda home=None: [tmp_path, fake_install]
    )
    monkeypatch.delenv(inst.ENV_VAR, raising=False)
    assert inst.find_install() == fake_install
    monkeypatch.setenv(inst.ENV_VAR, str(fake_install))
    monkeypatch.setattr(inst, "candidate_folders", lambda home=None: [])
    assert inst.find_install() == fake_install


def test_candidate_folders_cover_steam_and_gog_on_both_systems(tmp_path):
    folders = [p.as_posix() for p in inst.candidate_folders(tmp_path)]
    steam = "steamapps/common/STAR WARS X-Wing vs TIE Fighter"
    assert tmp_path.as_posix() + "/.local/share/Steam/" + steam in folders
    assert "C:/Program Files (x86)/Steam/" + steam in folders
    assert any(f.startswith("C:/GOG Games/") for f in folders)
    assert any(f.startswith(tmp_path.as_posix() + "/GOG Games/") for f in folders)


def test_list_missions_by_folder(fake_install):
    listing = inst.list_missions(fake_install)
    assert list(listing) == [
        "Train",
        "Combat",
        "Melee",
        "BalanceOfPower/TRAIN",
        "BalanceOfPower/MELEE",
    ]
    assert [p.name for p in listing["Train"]] == ["1TA01BF.TIE", "ONLYXVT.TIE"]
    assert [p.name for p in listing["BalanceOfPower/TRAIN"]] == ["1ta01bf.tie"]
    assert all("Battle" not in str(p) for files in listing.values() for p in files)

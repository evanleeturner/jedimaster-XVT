"""The command line: ``dump`` and ``export``.

Purpose:
    Prove ``python -m jedimaster dump`` prints the rendering (by file path
    and by game path in an install) and ``export`` writes one valid JSON file
    per mission, by folder, with the right exit statuses.

Flow:
    Synthetic missions written into a temporary install; ``main`` called
    in-process; stdout captured.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_cli.py``
"""

from __future__ import annotations

import json
import logging

from builder import build_mission

from jedimaster.__main__ import main
from jedimaster.mission import read_mission
from jedimaster.mission import render_mission

logger = logging.getLogger(__name__)


def make_install(tmp_path):
    """Return a temporary install with two good missions and one bad file."""
    root = tmp_path / "XvT"
    (root / "Train").mkdir(parents=True)
    (root / "BalanceOfPower" / "MELEE").mkdir(parents=True)
    (root / "Train" / "A.TIE").write_bytes(
        build_mission(12, flight_groups=[{"name": "Alpha"}])
    )
    (root / "BalanceOfPower" / "MELEE" / "b.tie").write_bytes(build_mission(14))
    (root / "Train" / "BAD.TIE").write_bytes(b"\x0d\x00")
    return root


def test_dump_by_path(tmp_path, capsys):
    root = make_install(tmp_path)
    assert main(["dump", str(root / "Train" / "A.TIE")]) == 0
    expected = render_mission(read_mission(root / "Train" / "A.TIE"))
    assert capsys.readouterr().out == expected


def test_dump_by_game_path(tmp_path, capsys):
    root = make_install(tmp_path)
    assert main(["dump", "train\\a.tie", "--install", str(root)]) == 0
    assert '"Alpha"' in capsys.readouterr().out


def test_dump_refusals(tmp_path, capsys):
    root = make_install(tmp_path)
    assert main(["dump", str(root / "Train" / "BAD.TIE")]) == 1
    assert main(["dump", "train\\none.tie", "--install", str(root)]) == 2
    assert capsys.readouterr().out == ""


def test_export_writes_one_json_per_mission(tmp_path, capsys):
    root = make_install(tmp_path)
    out = tmp_path / "out"
    assert main(["export", str(root), str(out)]) == 1  # BAD.TIE fails
    assert "exported 2 missions" in capsys.readouterr().out
    first = json.loads((out / "Train" / "A.json").read_text(encoding="utf-8"))
    assert first["flight_groups"][0]["name"] == "Alpha"
    second = json.loads(
        (out / "BalanceOfPower" / "MELEE" / "b.json").read_text(encoding="utf-8")
    )
    assert second["header"]["platform_id"] == 14
    assert not (out / "Train" / "BAD.json").exists()
    (root / "Train" / "BAD.TIE").unlink()
    assert main(["export", str(root), str(out)]) == 0


def test_export_needs_an_install(tmp_path, capsys):
    assert main(["export", str(tmp_path), str(tmp_path / "out")]) == 2

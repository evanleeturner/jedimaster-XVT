"""The ``icons`` command line: dump one bitmap, export an install's icons.

Purpose:
    Prove ``python -m jedimaster icons dump`` prints the reader's view of a
    file or a game path, ``icons export`` writes ``icons.json`` and one PNG
    per sheet with or without Balance of Power, and both exit with the
    package's statuses on missing or unreadable input.

Flow:
    Build a fake install with ``iconinstall``; call ``main`` with arguments.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_icons_cli.py``
"""

from __future__ import annotations

import json
import logging

from bmpdata import bmp
from bmpdata import end
from bmpdata import run
from iconinstall import make_install
from iconinstall import NAMES

from jedimaster.__main__ import main
from jedimaster.icons import read_bmp
from jedimaster.icons import render_bmp

logger = logging.getLogger(__name__)


def test_icons_dump_by_path_and_by_game_path(tmp_path, capsys):
    path = tmp_path / "x.bmp"
    path.write_bytes(bmp(4, 1, run(4, 3) + end()))
    assert main(["icons", "dump", str(path)]) == 0
    assert capsys.readouterr().out == render_bmp(read_bmp(path))
    root = make_install(tmp_path / "XvT")
    assert main(["icons", "dump", "frontres\\S3.bmp", "--install", str(root)]) == 0
    assert "width 320" in capsys.readouterr().out.splitlines()


def test_icons_dump_statuses(tmp_path):
    bad = tmp_path / "bad.bmp"
    bad.write_bytes(b"BM" + b"\0" * 2000)
    assert main(["icons", "dump", str(bad)]) == 1
    root = make_install(tmp_path / "XvT")
    assert main(["icons", "dump", "frontres\\none.bmp", "--install", str(root)]) == 2


def test_icons_export_writes_json_and_pictures(tmp_path, capsys):
    root = make_install(tmp_path / "XvT")
    out = tmp_path / "out"
    assert main(["icons", "export", str(root), str(out)]) == 0
    assert "exported 6 icon sheets" in capsys.readouterr().out
    data = json.loads((out / "icons.json").read_text(encoding="utf-8"))
    assert data["balance_of_power"] is True
    pictures = sorted(p.name for p in out.glob("*.png"))
    assert pictures == sorted(f"{name}.png" for name in NAMES)
    assert [s["picture"] for s in data["sheets"]] == [
        f"{n}.png" for n in ("greyicon", *NAMES[:5])
    ]


def test_icons_export_without_balance_of_power(tmp_path):
    root = make_install(tmp_path / "XvT")
    out = tmp_path / "out"
    assert main(["icons", "export", str(root), str(out), "--no-balance-of-power"]) == 0
    data = json.loads((out / "icons.json").read_text(encoding="utf-8"))
    assert data["balance_of_power"] is False
    assert all(s["file"].startswith("frontres/") for s in data["sheets"])


def test_icons_export_statuses(tmp_path):
    assert main(["icons", "export", str(tmp_path / "none"), str(tmp_path / "o")]) == 2
    empty = tmp_path / "XvT"
    (empty / "Train").mkdir(parents=True)
    assert main(["icons", "export", str(empty), str(tmp_path / "o")]) == 2
    root = make_install(tmp_path / "Full")
    blocked = tmp_path / "file"
    blocked.write_bytes(b"")
    assert main(["icons", "export", str(root), str(blocked)]) == 1

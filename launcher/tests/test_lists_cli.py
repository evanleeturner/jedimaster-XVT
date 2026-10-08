"""The command line's ``lists dump`` and ``lists export``.

Purpose:
    Prove ``python -m jedimaster lists dump`` prints the reader's own view
    (by file path, by game path in an install, with ``--kind``) and ``lists
    export`` writes one valid JSON file per list, with the right exit
    statuses.

Flow:
    Synthetic lists written into a temporary install; ``main`` called
    in-process; stdout captured.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_lists_cli.py``
"""

from __future__ import annotations

import json
import logging

from listdata import crlf

from jedimaster.__main__ import main
from jedimaster.lists import read_menu
from jedimaster.lists import render_raw

logger = logging.getLogger(__name__)


def make_install(tmp_path):
    """Return a temporary install with a menu, a sequence and a bad list."""
    root = tmp_path / "XvT"
    (root / "Train").mkdir(parents=True)
    (root / "BalanceOfPower" / "BATTLE").mkdir(parents=True)
    (root / "Train" / "MISSION.LST").write_bytes(crlf("[S]", "1", "a.tie", "Alpha"))
    (root / "BalanceOfPower" / "BATTLE" / "b1.lst").write_bytes(
        crlf("1", "a.tie", "Text")
    )
    (root / "Train" / "REBEL.LST").write_bytes(b"1\r\n\0")
    (root / "Train" / "odd.txt").write_bytes(crlf("1"))
    return root


def test_lists_dump_by_path(tmp_path, capsys):
    root = make_install(tmp_path)
    assert main(["lists", "dump", str(root / "Train" / "MISSION.LST")]) == 0
    expected = render_raw(read_menu(root / "Train" / "MISSION.LST"))
    assert capsys.readouterr().out == expected


def test_lists_dump_by_game_path_and_kind(tmp_path, capsys):
    root = make_install(tmp_path)
    assert main(["lists", "dump", "battle\\B1.LST", "--install", str(root)]) == 0
    assert 'kind "sequence"' in capsys.readouterr().out
    odd = str(root / "Train" / "odd.txt")
    assert main(["lists", "dump", odd, "--kind", "sequence"]) == 0
    assert "count 1" in capsys.readouterr().out


def test_lists_dump_refusals(tmp_path, capsys):
    root = make_install(tmp_path)
    assert main(["lists", "dump", str(root / "Train" / "REBEL.LST")]) == 1
    assert main(["lists", "dump", "train\\none.lst", "--install", str(root)]) == 2
    assert main(["lists", "dump", str(root / "Train" / "odd.txt")]) == 2
    assert capsys.readouterr().out == ""


def test_lists_export_writes_one_json_per_list(tmp_path, capsys):
    root = make_install(tmp_path)
    out = tmp_path / "out"
    assert main(["lists", "export", str(root), str(out)]) == 1  # REBEL.LST fails
    assert "exported 2 lists" in capsys.readouterr().out
    menu = json.loads((out / "Train" / "MISSION.json").read_text(encoding="utf-8"))
    assert menu["kind"] == "menu" and menu["items"][1]["title_line"]["text"] == "Alpha"
    seq = json.loads(
        (out / "BalanceOfPower/BATTLE/b1.json").read_text(encoding="utf-8")
    )
    assert seq["description"] == "Text\r\n"
    assert not (out / "Train" / "REBEL.json").exists()
    (root / "Train" / "REBEL.LST").unlink()
    assert main(["lists", "export", str(root), str(out)]) == 0


def test_lists_export_needs_an_install(tmp_path):
    assert main(["lists", "export", str(tmp_path), str(tmp_path / "out")]) == 2

"""The start-up comparison's own pieces: state lines, differences, pilot records.

Purpose:
    Prove the parts of ``tools/compare_launch.py``: reading a state file,
    naming each state value that differs, naming each pilot member that
    differs by the launcher's layout, and the exit status.

Flow:
    Made-up state files and made-up pilot records (all zero bytes, then one
    member changed through the layout's own offsets).

Invariants:
    - No game data.
    - Identical recordings give exit status 0 and the word ``identical``;
      every other case gives 1.

Call:
    ``pytest tests/test_compare_launch.py``
"""

from __future__ import annotations

import logging
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import compare_launch as cl  # noqa: E402

from jedimaster.pilot import FULL_RECORD  # noqa: E402
from jedimaster.pilot import locate  # noqa: E402
from jedimaster.pilot import struct as pilot_struct  # noqa: E402

logger = logging.getLogger(__name__)

SIZE = pilot_struct(FULL_RECORD).size


def line(name: str, raw: bytes) -> str:
    """Return one state line."""
    return f"{name} {len(raw)} {raw.hex()}"


def whole(value: int) -> bytes:
    """Return a 4-byte little-endian whole number."""
    return struct.pack("<i", value)


def write(
    tmp_path: Path, name: str, lines: list[str], pilot: bytes | None = None
) -> Path:
    """Write a recording (state file and pilot file) and return its path."""
    path = tmp_path / name
    path.write_text("\n".join(lines) + "\n", encoding="ascii")
    Path(f"{path}.pilot").write_bytes(pilot if pilot is not None else bytes(SIZE))
    return path


def with_member(path: tuple[str | int, ...], value: int) -> bytes:
    """Return an all-zero pilot record with one whole-number member set."""
    offset, member = locate(FULL_RECORD, path)
    data = bytearray(SIZE)
    data[offset : offset + 4] = whole(value)
    return bytes(data)


STATE = [
    line("session_mode", whole(2)),
    line("is_host", whole(1)),
    line("launch_line", b"~train\\a.tie~ 1"),
]


def test_a_state_line_is_name_size_and_hex(tmp_path):
    state = cl.read_state(write(tmp_path, "a", STATE))
    assert state["session_mode"] == whole(2)
    assert state["launch_line"] == b"~train\\a.tie~ 1"


def test_a_bad_state_line_is_a_difference_not_a_crash(tmp_path):
    menus = write(tmp_path, "a", STATE)
    door = tmp_path / "b"
    for text in ("name", "name x 00", "name 2 00", "name 1 0g"):
        door.write_text(text + "\n", encoding="ascii")
        Path(f"{door}.pilot").write_bytes(bytes(SIZE))
        found = cl.compare(menus, door, set())
        assert found and found[0].startswith("state:"), text


def test_identical_recordings_have_no_difference_and_exit_zero(tmp_path, capsys):
    menus = write(tmp_path, "a", STATE)
    door = write(tmp_path, "b", STATE)
    assert cl.compare(menus, door, set()) == []
    assert cl.main([str(menus), str(door)]) == 0
    assert capsys.readouterr().out == "identical\n"


def test_each_differing_state_value_is_named_with_both_values(tmp_path, capsys):
    menus = write(tmp_path, "a", STATE)
    other = [
        STATE[0],
        line("is_host", whole(0)),
        line("launch_line", b"~train\\b.tie~ 1"),
    ]
    door = write(tmp_path, "b", other)
    found = cl.compare(menus, door, set())
    assert found == [
        "is_host: menus 1 door 0",
        "launch_line: menus '~train\\\\a.tie~ 1' door '~train\\\\b.tie~ 1'",
    ]
    assert cl.main([str(menus), str(door)]) == 1
    assert capsys.readouterr().out.endswith("2 differences\n")


def test_a_line_only_one_side_holds_is_a_difference(tmp_path):
    menus = write(tmp_path, "a", STATE)
    door = write(tmp_path, "b", STATE[:2])
    assert cl.compare(menus, door, set()) == [
        "launch_line: only the menus recording holds it"
    ]
    assert cl.compare(door, menus, set()) == [
        "launch_line: only the door recording holds it"
    ]


def test_an_ignored_name_is_left_out(tmp_path):
    menus = write(tmp_path, "a", STATE)
    door = write(tmp_path, "b", [STATE[0], line("is_host", whole(0)), STATE[2]])
    assert cl.compare(menus, door, {"is_host"}) == []


def test_a_differing_pilot_member_is_named_by_the_layout(tmp_path):
    menus = write(tmp_path, "a", STATE, with_member(("total_score",), 5))
    door = write(tmp_path, "b", STATE, with_member(("total_score",), 9))
    found = cl.compare(menus, door, set())
    assert len(found) == 1
    assert found[0].startswith("pilot total_score: menus [5] door [9]")
    assert cl.compare(menus, door, {"total_score"}) == []


def test_a_member_zero_on_one_side_reads_zero(tmp_path):
    menus = write(tmp_path, "a", STATE)
    door = write(
        tmp_path, "b", STATE, with_member(("faction_statistics", 2, "team"), 3)
    )
    found = cl.compare(menus, door, set())
    assert found and all("faction_statistics[2]" in text for text in found)


def test_a_pilot_file_of_the_wrong_size_is_a_difference(tmp_path):
    menus = write(tmp_path, "a", STATE)
    door = write(tmp_path, "b", STATE, bytes(SIZE - 1))
    found = cl.compare(menus, door, set())
    assert found == [f"pilot record: sizes {SIZE} and {SIZE - 1}, the layout is {SIZE}"]


def test_a_missing_pilot_file_is_a_difference(tmp_path):
    menus = write(tmp_path, "a", STATE)
    door = write(tmp_path, "b", STATE)
    Path(f"{door}.pilot").unlink()
    assert cl.compare(menus, door, set())[0].startswith("pilot record:")


def test_a_long_value_is_cut(tmp_path):
    assert cl.value_of({"x": "x = " + "9" * 100}, "x") == "9" * 57 + "..."
    assert cl.value_of({}, "x") == "zero"


def test_a_value_reads_as_text_a_number_or_hex():
    assert cl.show(whole(-3)) == "-3"
    assert cl.show(b"Ace\0\0\0\0\0\0") == "'Ace'"
    assert cl.show(b"\x01\x02\x03") == "010203"

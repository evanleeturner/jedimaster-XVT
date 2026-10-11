"""Name every difference between two recordings of the state a battle starts from.

Purpose:
    The proof that a battle started by the ``--launch`` door begins from the
    same state as one started through the menus: the engine's
    ``--record-launch`` option writes a state file and a pilot record file,
    and this tool compares two of them field by field.

Flow:
    1. Read both state files: one line each, ``name size hex``.
    2. Print each line whose bytes differ, with both values (whole numbers
       for 1, 2, 4 and 8 bytes, text for names, hex otherwise), and each
       line only one side holds.
    3. Read both pilot record files by the launcher's pilot layout, print
       each member whose value differs, then, when nothing is ignored, any
       differing byte that no member of the layout holds.
    4. Print one summary line.

Invariants:
    - Exit status 0 only when no state line and no pilot byte differs.
    - A pilot file of the wrong size, or a state line that cannot be read,
      is a difference, never a crash.
    - Nothing is written.

Call:
    ``python tools/compare_launch.py MENUS DOOR [--ignore NAME ...]``
    where MENUS and DOOR are the paths given to ``--record-launch``
    (``<path>`` and ``<path>.pilot`` are read).
"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from jedimaster.pilot import FULL_RECORD  # noqa: E402
from jedimaster.pilot import read_record  # noqa: E402
from jedimaster.pilot import record_lines  # noqa: E402
from jedimaster.pilot import struct as pilot_struct  # noqa: E402

logger = logging.getLogger("compare_launch")

WHOLE_NUMBER_SIZES = (1, 2, 4, 8)


def read_state(path: Path) -> dict[str, bytes]:
    """Return the state file's lines as name -> bytes; raise ValueError on a bad line."""
    values: dict[str, bytes] = {}
    for number, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        parts = line.split(" ")
        if len(parts) not in (2, 3) or not parts[1].isdigit():
            raise ValueError(f"{path}:{number}: not a 'name size hex' line")
        raw = bytes.fromhex(parts[2]) if len(parts) == 3 else b""
        if len(raw) != int(parts[1]):
            raise ValueError(f"{path}:{number}: {parts[0]} holds {len(raw)} bytes")
        values[parts[0]] = raw
    return values


def show(raw: bytes) -> str:
    """Return a value as a whole number, text or hex, whichever reads best."""
    if len(raw) in WHOLE_NUMBER_SIZES:
        return str(int.from_bytes(raw, "little", signed=True))
    text = raw.split(b"\0", 1)[0]
    if text and all(32 <= byte < 127 for byte in text) and not any(raw[len(text) :]):
        return repr(text.decode("ascii"))
    return raw.hex()


def state_differences(
    a: dict[str, bytes], b: dict[str, bytes], ignore: set[str]
) -> list[str]:
    """Return one line for each state value that differs or that only one side holds."""
    lines = []
    for name in sorted(set(a) | set(b)):
        if name in ignore:
            continue
        if name not in a or name not in b:
            side = "menus" if name in a else "door"
            lines.append(f"{name}: only the {side} recording holds it")
        elif a[name] != b[name]:
            lines.append(f"{name}: menus {show(a[name])} door {show(b[name])}")
    return lines


def pilot_lines(data: bytes) -> dict[str, str]:
    """Return the pilot record's layout lines as name -> line."""
    record = read_record(data, FULL_RECORD)
    return {line.split(" = ", 1)[0]: line for line in record_lines(FULL_RECORD, record)}


def value_of(lines: dict[str, str], name: str) -> str:
    """Return a pilot member's value text, cut to 60 characters, or 'zero' when absent."""
    if name not in lines:
        return "zero"
    value = lines[name][len(name) + 3 :]
    return value if len(value) <= 60 else value[:57] + "..."


def pilot_differences(a: bytes, b: bytes, ignore: set[str]) -> list[str]:
    """Return one line for each pilot member that differs, then any unnamed bytes."""
    size = pilot_struct(FULL_RECORD).size
    if len(a) != size or len(b) != size:
        return [f"pilot record: sizes {len(a)} and {len(b)}, the layout is {size}"]
    lines = []
    left, right = pilot_lines(a), pilot_lines(b)
    for name in sorted(set(left) | set(right)):
        if name in ignore or left.get(name) == right.get(name):
            continue
        lines.append(
            f"pilot {name}: menus [{value_of(left, name)}] door [{value_of(right, name)}]"
        )
    named = bool(lines)
    differing = [at for at in range(size) if a[at] != b[at]]
    if differing and not named and not ignore:
        first = differing[0]
        lines.append(
            f"pilot record: {len(differing)} bytes differ outside every named member or only in a zero struct element; first at offset {first}"
        )
    return lines


def compare(menus: Path, door: Path, ignore: set[str]) -> list[str]:
    """Return every difference between two recordings; empty when they are the same."""
    try:
        lines = state_differences(read_state(menus), read_state(door), ignore)
    except (OSError, ValueError) as error:
        return [f"state: {error}"]
    try:
        pilots = [Path(f"{path}.pilot").read_bytes() for path in (menus, door)]
    except OSError as error:
        return lines + [f"pilot record: {error}"]
    return lines + pilot_differences(pilots[0], pilots[1], ignore)


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when the recordings are the same."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("menus", type=Path)
    parser.add_argument("door", type=Path)
    parser.add_argument("--ignore", action="append", default=[], metavar="NAME")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.WARNING)
    differences = compare(args.menus, args.door, set(args.ignore))
    for line in differences:
        print(line)
    print(f"{len(differences)} differences" if differences else "identical")
    return 1 if differences else 0


if __name__ == "__main__":
    sys.exit(main())

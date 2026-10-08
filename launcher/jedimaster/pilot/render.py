"""Print the layout and a pilot's records in the answer sheets' text format.

Purpose:
    Render the three sheet kinds: ``kind layout`` (every struct and
    member of the layout), ``kind raw`` (one file read whole as its
    record) and ``kind load`` (the record the game holds after a load,
    with which files were there and the load's result).

Flow:
    ``render_layout`` walks ``STRUCTS``; ``record_lines`` walks a struct's
    members with a record's values; ``render_raw`` and ``render_load`` put
    their header lines before ``record_lines``.

Invariants:
    - A line is ``PATH = VALUES``: struct members joined by ``.``, an
      element of an array of structs as ``[i]`` (``[i][j]`` for more
      dimensions); a struct member's members print in place.
    - ``i32`` values print signed, ``u32`` unsigned, in decimal, every
      element of an array in a row, the last index fastest, each after a
      space; a ``u8`` array prints after one space as two lowercase hex
      digits a byte; a ``char`` array prints quoted up to its first byte 0,
      then `` hex=`` and all its bytes.
    - An element of an array of structs whose bytes are all 0 is left out;
      every other member prints, zeros included.
    - Every sheet ends with one newline.

Call:
    ``sys.stdout.write(render_load(load))``
"""

from __future__ import annotations

import logging
from itertools import product
from typing import Any

from ..text.sheet import join
from ..text.sheet import quote
from .layout import CHAR
from .layout import FULL_RECORD
from .layout import is_struct
from .layout import Member
from .layout import struct
from .layout import STRUCTS
from .layout import U8
from .load import PilotLoad
from .record import Text

logger = logging.getLogger(__name__)


def render_layout() -> str:
    """Return the layout sheet: ``kind layout``, then every struct and member.

    ``struct <name> size=<bytes>``, then per member ``member <name>
    offset=<bytes> type=<type> dims=<-|[a][b]...> size=<bytes>``, in the
    layout's order. Always returns the same text. Does not check the
    layout.
    """
    lines = ["kind layout"]
    for item in STRUCTS:
        lines.append(f"struct {item.name} size={item.size}")
        for m in item.members:
            dims = "".join(f"[{d}]" for d in m.dims) or "-"
            lines.append(
                f"member {m.name} offset={m.offset} type={m.type} dims={dims} "
                f"size={m.size}"
            )
    return join(lines)


def flatten(value: Any) -> list[Any]:
    """Return the leaves of nested lists in order, the last index fastest.

    A value that is not a list is its own one leaf. Does not look inside
    dicts, ``bytes`` or ``Text``.
    """
    if not isinstance(value, list):
        return [value]
    return [leaf for item in value for leaf in flatten(item)]


def is_zero(value: Any) -> bool:
    """Return True when every byte a record value stands for is 0.

    Integers are 0, ``bytes`` and ``Text`` hold only byte 0, lists and
    dicts hold only such values. Does not check types it does not know
    (they count as not zero).
    """
    if isinstance(value, int):
        return value == 0
    if isinstance(value, bytes):
        return not any(value)
    if isinstance(value, Text):
        return not any(value.raw)
    if isinstance(value, list):
        return all(is_zero(item) for item in value)
    if isinstance(value, dict):
        return all(is_zero(item) for item in value.values())
    return False


def _value_line(path: str, member: Member, value: Any) -> str:
    leaves = flatten(value)
    if member.type == U8:
        raw = b"".join(bytes([v]) if isinstance(v, int) else v for v in leaves)
        return f"{path} = {raw.hex()}"
    if member.type == CHAR:
        raw = b"".join(text.raw for text in leaves)
        return f"{path} = {quote(raw)} hex={raw.hex()}"
    return f"{path} = " + " ".join(str(v) for v in leaves)


def _element(value: Any, index: tuple[int, ...]) -> Any:
    for i in index:
        value = value[i]
    return value


def record_lines(name: str, record: dict[str, Any], prefix: str = "") -> list[str]:
    """Return the sheet lines of a record of struct ``name``.

    ``prefix`` goes before each member's name (``"teams[3]."``). Struct
    members print their members in place; elements of struct arrays whose
    bytes are all 0 are left out. Raises ``KeyError`` when ``record``
    lacks a member of the layout. Does not check the values' types or
    ranges.
    """
    lines: list[str] = []
    for member in struct(name).members:
        value = record[member.name]
        path = prefix + member.name
        if not is_struct(member.type):
            lines.append(_value_line(path, member, value))
        elif not member.dims:
            lines += record_lines(member.type, value, path + ".")
        else:
            for index in product(*(range(d) for d in member.dims)):
                element = _element(value, index)
                if is_zero(element):
                    continue
                where = "".join(f"[{i}]" for i in index)
                lines += record_lines(member.type, element, f"{path}{where}.")
    return lines


def render_raw(name: str, record: dict[str, Any], read: int) -> str:
    """Return the raw sheet of one file read whole as record ``name``.

    ``kind raw``, ``bytes <read> of <record size>``, then the record's
    lines. Does not check that ``read`` is the record's size.
    """
    header = ["kind raw", f"bytes {read} of {struct(name).size}"]
    return join(header + record_lines(name, record))


def render_load(load: PilotLoad) -> str:
    """Return the load sheet of a ``PilotLoad``.

    ``kind load``, ``files pl2=<1/0> plt=<1/0>``, ``result <1/0>``, then
    the full record's lines. Does not check the load.
    """
    header = [
        "kind load",
        f"files pl2={int(load.pl2)} plt={int(load.plt)}",
        f"result {load.result}",
    ]
    return join(header + record_lines(FULL_RECORD, load.record))

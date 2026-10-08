"""Read joystick.txt, the joystick action names, as the game reads it.

Purpose:
    Turn every line of the file into an action (a code byte, a name and a
    description) by the game's own steps over its line buffer, so a line
    without a space reads its name and description from the bytes earlier,
    longer lines left in the buffer, exactly as the game shows them.

Flow:
    ``read_joystick`` reads pieces of buffer 128 (comments kept: every line
    is an entry), loads each into a 256-byte ``LineBuffer`` and runs
    ``parse_action`` on it: the code up to the first space or end mark, one
    byte passed over, the name up to the next space or end mark, the
    description after the byte that ended the name, up to the next end
    mark.

Invariants:
    - The code's value is ``leading_number`` of its bytes (leading blanks
      and a sign allowed, none gives 0), modulo 256.
    - The buffer starts all zero; past each piece and its end mark it keeps
      what earlier pieces left.
    - A name over ``NAME_ROOM`` bytes or a description over
      ``DESCRIPTION_ROOM`` bytes is kept whole, with a WARNING: the game
      checks neither.

Call:
    ``actions = read_joystick(path).actions; actions[1].name``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass

from .lines import LineBuffer
from .lines import LineStream
from .lines import load

logger = logging.getLogger(__name__)

PIECE_BUFFER = 128
LINE_BUFFER = 256
CODE_LIMIT = 256
NAME_ROOM = 20
DESCRIPTION_ROOM = 128
SPACE = 0x20
END_MARK = 0x00
OLD_END_OF_FILE = 0x1A
BLANKS = frozenset(b" \t\n\v\f\r")
"""The bytes skipped before a number: the C library's white space."""
SIGNS = {ord("+"): 1, ord("-"): -1}


@dataclass(frozen=True)
class JoystickAction:
    """One action: its code byte, name and description, and its file line."""

    code: int
    name: bytes
    description: bytes
    line: int


@dataclass
class JoystickFile:
    """The actions in file order, numbered from 0."""

    actions: list[JoystickAction]


def leading_number(text: bytes) -> int:
    """Return the decimal number ``text`` starts with, as the C library reads it.

    Leading blanks (``BLANKS``) are skipped, then one ``+`` or ``-``, then
    digits; no digit gives 0. The value is exact (callers take it modulo
    what they keep). Does not stop at an end mark: callers pass the bytes.
    """
    i = 0
    while i < len(text) and text[i] in BLANKS:
        i += 1
    sign = 1
    if i < len(text) and text[i] in SIGNS:
        sign = SIGNS[text[i]]
        i += 1
    start = i
    while i < len(text) and 0x30 <= text[i] <= 0x39:
        i += 1
    return sign * int(text[start:i]) if i > start else 0


def _token(buffer: LineBuffer, start: int, limit: int) -> int:
    """Return where the run from ``start`` meets a space or an end mark."""
    end = start
    while end - start < limit and buffer.byte(end) not in (SPACE, END_MARK):
        end += 1
    return end


def parse_action(buffer: LineBuffer, number: int) -> JoystickAction:
    """Return the action the game reads from a loaded line buffer.

    The code is the run from byte 0 to the first space or end mark (at most
    256 bytes); one byte is passed over; the name runs to the next space or
    end mark; the description is the rest up to the next end mark, from the
    byte after the one that ended the name. Always returns an action. Does
    not check the name's or description's length.
    """
    code_end = _token(buffer, 0, CODE_LIMIT)
    code = leading_number(bytes(buffer.bytes[:code_end])) % 256
    name_start = code_end + 1
    name_end = _token(buffer, name_start, len(buffer.bytes))
    name = bytes(buffer.bytes[name_start:name_end])
    description = buffer.string(name_end + 1)
    return JoystickAction(code, name, description, number)


def report_action(action: JoystickAction, index: int, label: str) -> bool:
    """Log a WARNING for a name over 20 bytes or a description over 128.

    Returns True when it logged one. The game checks neither: the action is
    kept whole. Does not change the action.
    """
    long_name = len(action.name) > NAME_ROOM
    long_description = len(action.description) > DESCRIPTION_ROOM
    if long_name:
        logger.warning(
            "%s: action %d (line %d): a %d-byte name, room for %d",
            label,
            index,
            action.line,
            len(action.name),
            NAME_ROOM,
        )
    if long_description:
        logger.warning(
            "%s: action %d (line %d): a %d-byte description, room for %d",
            label,
            index,
            action.line,
            len(action.description),
            DESCRIPTION_ROOM,
        )
    return long_name or long_description


def read_joystick(source: str | os.PathLike[str] | bytes | bytearray) -> JoystickFile:
    """Return joystick.txt's actions as the game reads them.

    Every piece (buffer 128, comments included) is one action, read by
    ``parse_action`` from a 256-byte buffer that starts all zero and keeps
    earlier pieces' leftover bytes. Returns no actions for an empty file.
    Logs a WARNING for a name over 20 bytes or a description over 128.
    Raises ``OSError`` when the path cannot be read. Does not check codes
    for repeats.
    """
    data, label = load(source)
    stream = LineStream(data, PIECE_BUFFER)
    buffer = LineBuffer(LINE_BUFFER)
    actions = []
    while (piece := stream.read_piece()) is not None:
        buffer.load(piece)
        action = parse_action(buffer, stream.pieces)
        logger.debug(
            "action %d: code %d name %r description %r",
            len(actions),
            action.code,
            action.name,
            action.description,
        )
        if piece.startswith(bytes((OLD_END_OF_FILE,))):
            logger.info("%s: action %d holds byte 26", label, len(actions))
        report_action(action, len(actions), label)
        actions.append(action)
    logger.info("joystick %s: %d actions", label, len(actions))
    return JoystickFile(actions)

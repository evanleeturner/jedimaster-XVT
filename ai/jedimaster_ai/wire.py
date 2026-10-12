"""The bytes of the messages between the engine and the AI program.

Purpose:
    Turn the engine's questions into ``world`` classes and the program's
    answers into bytes, exactly as ``engine/src/xvt_runtime/ai/ai_wire.h``
    sets them out. The engine's half, writing questions and reading answers
    by the engine's rules, is here too, for tests and replays.

Flow:
    1. ``read_message`` takes one message off a stream: a u32 length, then
       that many bytes
    2. ``read_question`` checks the header and the length its craft count
       needs, then reads each craft view
    3. ``answer_bytes`` writes an answer, its length field first; actions go
       in kind order

Invariants:
    - Every value is little-endian on every machine.
    - A message that breaks the format raises WireError; nothing is guessed.
    - ``read_answer`` refuses what the engine's reader refuses: a wrong
      header, tick, count or craft, an unknown or repeated action kind, an
      activity past the last, a think interval of 0, and a short or long
      message.
"""

from __future__ import annotations

import dataclasses
import enum
import struct
import typing as t

from .world import Actions
from .world import Answer
from .world import CraftAnswer
from .world import CraftView
from .world import NOTEBOOK_BYTES
from .world import Question
from .world import Steer

MAGIC = 0x57504D4A  # "JMPW"
VERSION = 1
KIND_QUESTION = 1
KIND_ANSWER = 2
LENGTH = struct.Struct("<I")
# After the length field: magic, version, kind, tick, craft count, zero.
HEADER = struct.Struct("<IHHIHH")
HEAD_BYTES = LENGTH.size + HEADER.size
# A craft view's fields in the engine's order; the notebook follows them.
VIEW_FIELDS = struct.Struct("<HHBBBBBBHHHiiiHHHHh")
CRAFT_VIEW_BYTES = VIEW_FIELDS.size + NOTEBOOK_BYTES
U8 = struct.Struct("<B")
U16 = struct.Struct("<H")
STEER = struct.Struct("<HHH")
# The craft view's fields that VIEW_FIELDS packs, in its order.
VIEW_FIELD_NAMES = tuple(
    field.name for field in dataclasses.fields(CraftView) if field.name != "notebook"
)


class ActionKind(enum.IntEnum):
    """The action kinds of a craft answer, in the order they are written."""

    STEER = 1
    THROTTLE = 2
    ACTIVITY = 3
    THINK_INTERVAL = 4


class WireError(ValueError):
    """Bytes that break the message format."""


def read_message(stream: t.BinaryIO) -> bytes | None:
    """Return the next message's bytes after its length field.

    Returns None when the stream ends before a message starts.
    """
    head = stream.read(LENGTH.size)
    if not head:
        return None
    if len(head) < LENGTH.size:
        raise WireError(f"the stream ended {len(head)} bytes into a length")
    (length,) = LENGTH.unpack(head)
    body = stream.read(length)
    if len(body) < length:
        raise WireError(
            f"the stream ended {len(body)} bytes into a {length}-byte message"
        )
    return body


def _read_header(body: bytes, kind: int) -> tuple[int, int]:
    """Return the tick and craft count of a message of this kind."""
    if len(body) < HEADER.size:
        raise WireError(f"a message of {len(body)} bytes is shorter than its header")
    magic, version, found, tick, count, zero = HEADER.unpack_from(body)
    if (magic, version, found, zero) != (MAGIC, VERSION, kind, 0):
        raise WireError(
            f"the header magic={magic:#x} version={version} kind={found} "
            f"zero={zero} is not a version {VERSION} kind {kind} message's"
        )
    return tick, count


def read_question(body: bytes) -> Question:
    """Return the question these bytes, after the length field, hold."""
    tick, count = _read_header(body, KIND_QUESTION)
    expected = HEADER.size + count * CRAFT_VIEW_BYTES
    if len(body) != expected:
        raise WireError(
            f"a question about {count} craft holds {len(body)} bytes, not {expected}"
        )
    craft = []
    for offset in range(HEADER.size, expected, CRAFT_VIEW_BYTES):
        fields = VIEW_FIELDS.unpack_from(body, offset)
        notebook = body[offset + VIEW_FIELDS.size : offset + CRAFT_VIEW_BYTES]
        craft.append(CraftView(*fields, notebook=notebook))
    return Question(tick, tuple(craft))


def question_bytes(question: Question) -> bytes:
    """Return a question's bytes, its length field first, as the engine writes."""
    body = bytearray(
        HEADER.pack(
            MAGIC, VERSION, KIND_QUESTION, question.tick, len(question.craft), 0
        )
    )
    for view in question.craft:
        body += VIEW_FIELDS.pack(*(getattr(view, name) for name in VIEW_FIELD_NAMES))
        body += view.notebook
    return LENGTH.pack(len(body)) + bytes(body)


def _actions_bytes(actions: Actions) -> bytes:
    written = bytearray()
    count = 0
    if actions.steer is not None:
        written += U8.pack(ActionKind.STEER)
        written += STEER.pack(
            actions.steer.yaw, actions.steer.pitch, actions.steer.roll
        )
        count += 1
    if actions.throttle is not None:
        written += U8.pack(ActionKind.THROTTLE) + U16.pack(actions.throttle)
        count += 1
    if actions.activity is not None:
        written += U8.pack(ActionKind.ACTIVITY) + U8.pack(actions.activity)
        count += 1
    if actions.think_interval is not None:
        written += U8.pack(ActionKind.THINK_INTERVAL) + U16.pack(actions.think_interval)
        count += 1
    return U8.pack(count) + bytes(written)


def answer_bytes(answer: Answer) -> bytes:
    """Return an answer's bytes, its length field first."""
    body = bytearray(
        HEADER.pack(MAGIC, VERSION, KIND_ANSWER, answer.tick, len(answer.craft), 0)
    )
    for craft in answer.craft:
        body += U16.pack(craft.object_index)
        body += craft.notebook
        body += _actions_bytes(craft.actions)
    return LENGTH.pack(len(body)) + bytes(body)


class _Reader:
    """Reads values off a message, refusing a read past its end."""

    def __init__(self, body: bytes, offset: int) -> None:
        self.body = body
        self.offset = offset

    def take(self, shape: struct.Struct) -> tuple[int, ...]:
        if self.offset + shape.size > len(self.body):
            raise WireError(f"the answer ends inside a value at byte {self.offset}")
        values = shape.unpack_from(self.body, self.offset)
        self.offset += shape.size
        return values

    def take_bytes(self, count: int) -> bytes:
        if self.offset + count > len(self.body):
            raise WireError(f"the answer ends inside a notebook at byte {self.offset}")
        taken = self.body[self.offset : self.offset + count]
        self.offset += count
        return taken


def _read_actions(reader: _Reader) -> Actions:
    found: dict[str, t.Any] = {}
    (count,) = reader.take(U8)
    for _ in range(count):
        (code,) = reader.take(U8)
        try:
            kind = ActionKind(code)
        except ValueError:
            raise WireError(f"action kind {code} is not one of the four") from None
        name = kind.name.lower()
        if name in found:
            raise WireError(f"action kind {code} is sent twice")
        if kind == ActionKind.STEER:
            found[name] = Steer(*reader.take(STEER))
        elif kind == ActionKind.ACTIVITY:
            found[name] = reader.take(U8)[0]
        else:
            found[name] = reader.take(U16)[0]
    try:
        return Actions(**found)
    except ValueError as err:
        raise WireError(f"an action is out of range: {err}") from err


def read_answer(body: bytes, question: Question) -> Answer:
    """Return the answer these bytes hold, by the engine's rules for one."""
    tick, count = _read_header(body, KIND_ANSWER)
    if tick != question.tick:
        raise WireError(
            f"the answer's tick {tick} is not the question's {question.tick}"
        )
    if count != len(question.craft):
        raise WireError(
            f"the answer is about {count} craft, the question {len(question.craft)}"
        )
    reader = _Reader(body, HEADER.size)
    craft = []
    for view in question.craft:
        (object_index,) = reader.take(U16)
        if object_index != view.object_index:
            raise WireError(
                f"the answer names craft {object_index} where the question "
                f"has {view.object_index}"
            )
        notebook = reader.take_bytes(NOTEBOOK_BYTES)
        craft.append(CraftAnswer(object_index, notebook, _read_actions(reader)))
    if reader.offset != len(body):
        raise WireError(
            f"the answer has {len(body) - reader.offset} bytes past its end"
        )
    return Answer(tick, tuple(craft))

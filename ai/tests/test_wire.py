"""Fence for wire: the messages' bytes, read and written as the engine does.

The question bytes are the engine's own test's, read from
``engine/tests/test_ai_wire.c``, so both sides check one set of bytes.
"""

from __future__ import annotations

import io
import re
import struct

import pytest
from conftest import ENGINE

from jedimaster_ai import wire
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Answer
from jedimaster_ai.world import CraftAnswer
from jedimaster_ai.world import CraftView
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer

C_TEST = ENGINE / "tests" / "test_ai_wire.c"
TEST_TICK = 0x0A0B0C0D
# view_with_distinct_bytes in the C test: every field's bytes differ.
DISTINCT = CraftView(
    0x0102, 0x0304, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0C0B, 0x0E0D, 0x100F,
    0x14131211, -2, 0x1C1B1A19, 0x1E1D, 0x201F, 0x2221, 0x2423, -3,
    notebook=bytes(0x40 + byte for byte in range(NOTEBOOK_BYTES)),
)  # fmt: skip


def engine_question_bytes() -> bytes:
    """Return the C test's expected question, its notebook bytes added."""
    text = C_TEST.read_text()
    block = re.search(r"expected\[AI_WIRE_HEAD_BYTES \+ 38\] = \{(.*?)\};", text, re.S)
    head = bytes(int(value, 16) for value in block.group(1).split(","))
    return head + DISTINCT.notebook


def test_reads_the_engines_question() -> None:
    message = engine_question_bytes()
    assert wire.read_question(message[4:]) == Question(TEST_TICK, (DISTINCT,))


def test_writes_the_engines_question() -> None:
    assert (
        wire.question_bytes(Question(TEST_TICK, (DISTINCT,))) == engine_question_bytes()
    )


def good_answer() -> tuple[Question, Answer]:
    """The C test's good answer: craft 7 sends every kind, craft 9 none."""
    question = Question(
        TEST_TICK,
        tuple(
            CraftView(index, *([0] * 18), notebook=bytes(NOTEBOOK_BYTES))
            for index in (7, 9)
        ),
    )
    every = Actions(Steer(0x1234, 0x4000, 0xFFFF), 0x8000, Activity.DISABLED, 59)
    answer = Answer(
        TEST_TICK,
        (
            CraftAnswer(7, b"\x5a" * NOTEBOOK_BYTES, every),
            CraftAnswer(9, b"\xa5" * NOTEBOOK_BYTES),
        ),
    )
    return question, answer


def test_answer_bytes_read_back() -> None:
    question, answer = good_answer()
    message = wire.answer_bytes(answer)
    # The length the C test's check_good_answer expects.
    assert len(message) == wire.HEAD_BYTES + 82 + 3 + NOTEBOOK_BYTES
    assert wire.read_answer(message[4:], question) == answer


def test_answer_writes_actions_in_kind_order() -> None:
    _, answer = good_answer()
    message = wire.answer_bytes(answer)
    start = wire.HEAD_BYTES + 2 + NOTEBOOK_BYTES
    # Count 4; steer 0x1234 0x4000 0xFFFF; throttle 0x8000; activity 12;
    # think interval 59.
    assert message[start : start + 16] == bytes.fromhex(
        "040134120040ffff020080030c043b00"
    )


def test_answer_length_field_counts_the_rest() -> None:
    _, answer = good_answer()
    message = wire.answer_bytes(answer)
    assert struct.unpack_from("<I", message)[0] == len(message) - 4


def changed_header(message: bytes, offset: int, value: int) -> bytes:
    """Return a message's bytes after its length field, one byte changed."""
    body = bytearray(message[4:])
    body[offset] = value
    return bytes(body)


@pytest.mark.parametrize(
    ("offset", "value"),
    [(0, 0x4B), (4, 2), (6, 2), (14, 1)],
    ids=["magic", "version", "kind", "zero"],
)
def test_question_with_a_bad_header_is_refused(offset: int, value: int) -> None:
    with pytest.raises(wire.WireError, match="header"):
        wire.read_question(changed_header(engine_question_bytes(), offset, value))


def test_question_shorter_than_its_header_is_refused() -> None:
    with pytest.raises(wire.WireError, match="shorter than its header"):
        wire.read_question(engine_question_bytes()[4:10])


@pytest.mark.parametrize("cut", [-1, 1], ids=["short", "long"])
def test_question_of_the_wrong_length_is_refused(cut: int) -> None:
    body = engine_question_bytes()[4:]
    body = body[:cut] if cut < 0 else body + b"\0"
    with pytest.raises(wire.WireError, match="about 1 craft"):
        wire.read_question(body)


def answer_with_actions(actions: bytes) -> bytes:
    """Return an answer about craft 7 alone, with these action bytes."""
    head = wire.HEADER.pack(wire.MAGIC, wire.VERSION, wire.KIND_ANSWER, TEST_TICK, 1, 0)
    return head + struct.pack("<H", 7) + bytes(NOTEBOOK_BYTES) + actions


QUESTION_7 = Question(
    TEST_TICK, (CraftView(7, *([0] * 18), notebook=bytes(NOTEBOOK_BYTES)),)
)


@pytest.mark.parametrize(
    ("actions", "match"),
    [
        (b"\x01\x00", "not one of the four"),
        (b"\x01\x05", "not one of the four"),
        (b"\x02\x02\x01\x00\x02\x02\x00", "sent twice"),
        (b"\x01\x03\x0d", "out of range"),
        (b"\x01\x04\x00\x00", "out of range"),
        (b"\x02\x02\x01\x00", "ends inside a value"),
        (b"\x00\x00", "past its end"),
    ],
    ids=["kind 0", "kind 5", "twice", "activity 13", "interval 0", "short", "long"],
)
def test_answer_breaking_the_engines_rules_is_refused(
    actions: bytes, match: str
) -> None:
    with pytest.raises(wire.WireError, match=match):
        wire.read_answer(answer_with_actions(actions), QUESTION_7)


def test_answer_at_the_last_activity_and_interval_1_is_read() -> None:
    answer = wire.read_answer(
        answer_with_actions(b"\x02\x03\x0c\x04\x01\x00"), QUESTION_7
    )
    assert answer.craft[0].actions == Actions(
        activity=Activity.DISABLED, think_interval=1
    )


@pytest.mark.parametrize(
    ("tick", "count", "index", "match"),
    [
        (TEST_TICK + 1, 1, 7, "tick"),
        (TEST_TICK, 2, 7, "about 2 craft"),
        (TEST_TICK, 1, 8, "names craft 8"),
    ],
    ids=["tick", "count", "craft"],
)
def test_answer_about_other_craft_is_refused(
    tick: int, count: int, index: int, match: str
) -> None:
    head = wire.HEADER.pack(wire.MAGIC, wire.VERSION, wire.KIND_ANSWER, tick, count, 0)
    body = head + struct.pack("<H", index) + bytes(NOTEBOOK_BYTES) + b"\x00"
    with pytest.raises(wire.WireError, match=match):
        wire.read_answer(body, QUESTION_7)


def test_read_message_takes_messages_in_turn() -> None:
    stream = io.BytesIO(b"\x02\x00\x00\x00ab\x01\x00\x00\x00c")
    assert wire.read_message(stream) == b"ab"
    assert wire.read_message(stream) == b"c"
    assert wire.read_message(stream) is None


@pytest.mark.parametrize(
    ("data", "match"),
    [(b"\x02\x00", "into a length"), (b"\x05\x00\x00\x00abc", "into a 5-byte message")],
    ids=["length", "body"],
)
def test_read_message_refuses_a_cut_message(data: bytes, match: str) -> None:
    with pytest.raises(wire.WireError, match=match):
        wire.read_message(io.BytesIO(data))

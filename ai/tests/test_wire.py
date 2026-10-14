"""Fence for wire: the messages' bytes, read and written as the engine does.

The question bytes are the engine's own test's, read from
``engine/tests/test_ai_wire.c``, so both sides check one set of bytes.
"""

from __future__ import annotations

import dataclasses
import io
import re
import struct

import pytest
from conftest import ENGINE

from jedimaster_ai import wire
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Answer
from jedimaster_ai.world import Arrival
from jedimaster_ai.world import Books
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import Contact
from jedimaster_ai.world import CraftAnswer
from jedimaster_ai.world import CraftView
from jedimaster_ai.world import LinkMode
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import OrderMove
from jedimaster_ai.world import Question
from jedimaster_ai.world import Steer
from jedimaster_ai.world import Turret
from jedimaster_ai.world import Warhead

C_TEST = ENGINE / "tests" / "test_ai_wire.c"
TEST_TICK = 0x0A0B0C0D
# view_with_distinct_bytes in the C test: every field's bytes differ.
DISTINCT = CraftView(
    0x0102, 0x0304, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0C0B, 0x0E0D, 0x100F,
    0x14131211, -2, 0x1C1B1A19, 0x1E1D, 0x201F, 0x2221, 0x2423, -3,
    0x28272625, 0x2A29, 0x2B, 0x2C, (0x2D, 0x2E), (0x2F, 0x30), (0x31, 0x32),
    (0x3433, 0x3635), 0x3A393837, 0x3C3B, (0x3D, 0x3E, 0x3F, 0x40),
    (0x41, 0x42, 0x43, 0x44), 0x4645, 0x47, 0x48, 0x49, 0x9291, 0x4B4A, 0x4D4C,
    (0x51504F4E, 0x55545352, 0x59585756), (0x5D5C5B5A, 0x61605F5E, -6),
    (0x65646362, 0x69686766, 0x6D6C6B6A),
    notebook=bytes(0x40 + byte for byte in range(NOTEBOOK_BYTES)),
)  # fmt: skip
# contact_with_distinct_bytes in the C test.
DISTINCT_CONTACT = Contact(
    0x4241, 0x4443, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4E4D4C4B, 0x5251504F,
    -4, 0x5453, 0x5655, 0x5857, 0x5A59, 0x5C5B, -5, 0x605F5E5D, 0x64636261,
    0x68676665, 0x69, 0x6D6C6B6A,
)  # fmt: skip


def _c_bytes(text: str, name: str) -> bytes:
    block = re.search(rf"{name}\[[^\]]*\] = \{{(.*?)\}};", text, re.S)
    return bytes(int(value, 16) for value in block.group(1).split(","))


def engine_question_bytes() -> bytes:
    """Return the C test's expected question, its notebook bytes added."""
    text = C_TEST.read_text()
    head = _c_bytes(text, "expected")
    return head + DISTINCT.notebook + _c_bytes(text, "expected_contacts")


DISTINCT_QUESTION = Question(TEST_TICK, (DISTINCT,), (DISTINCT_CONTACT,))


def test_reads_the_engines_question() -> None:
    message = engine_question_bytes()
    assert wire.read_question(message[4:]) == DISTINCT_QUESTION


def test_writes_the_engines_question() -> None:
    assert wire.question_bytes(DISTINCT_QUESTION) == engine_question_bytes()


def plain_view(index: int, **changes: object) -> CraftView:
    """Return a craft view of object index, every other field 0."""
    fields = {
        name: (0,) * count if count > 1 else 0 for name, _, count in wire.VIEW_LAYOUT
    }
    fields.update(object_index=index, **changes)
    return CraftView(**fields, notebook=bytes(NOTEBOOK_BYTES))


def plain_contact(index: int) -> Contact:
    """Return a contact of object index, every other field 0."""
    return Contact(index, *([0] * (len(wire.CONTACT_LAYOUT) - 1)))


# The C test's g_question: craft 7 with two cannon groups, launcher 0 ready
# and turrets in weapon slots 3 and 5, leading, with orders in slots 0 and 1,
# at its hangar; craft 9 with none of these; contacts 7, 9, 12.
CRAFT_7 = plain_view(
    7,
    turret_slot_mask=1 << 3 | 1 << 5,
    cannon_group_count=2,
    warhead_launcher_count=1,
    order_codes=(1, 2, 0, 0),
    leader=NO_OBJECT,
    books=Books.AT_HANGAR,
)
CONTACTS = tuple(plain_contact(index) for index in (7, 9, 12))


def good_answer() -> tuple[Question, Answer]:
    """The C test's good answer: craft 7 sends every kind but depart, craft 9
    none."""
    question = Question(TEST_TICK, (CRAFT_7, plain_view(9)), CONTACTS)
    every = Actions(
        Steer(0x1234, 0x4000, 0xFFFF),
        0x8000,
        Activity.DISABLED,
        59,
        target=12,
        lasers=(CannonGroup(LinkMode.ALL, 4), CannonGroup(LinkMode.ONE_AT_A_TIME, 2)),
        warhead=Warhead(0, paired=True),
        turrets=(Turret(5, NO_OBJECT), Turret(3, 12)),
        arrival=Arrival(True, 3600),
        order=OrderMove(1, 2),
        dock=True,
    )
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
    assert len(message) == wire.HEAD_BYTES + 109 + 3 + NOTEBOOK_BYTES
    assert wire.read_answer(message[4:], question) == answer


def test_answer_writes_actions_in_kind_order() -> None:
    _, answer = good_answer()
    message = wire.answer_bytes(answer)
    start = wire.HEAD_BYTES + 2 + NOTEBOOK_BYTES
    # Count 11; steer 0x1234 0x4000 0xFFFF; throttle 0x8000; activity 12;
    # think interval 59; target 12; lasers 3 4 1 2; warhead 0 paired;
    # turrets: slot 5 none, slot 3 at 12; arriving at 3,600; order slot 1
    # state 2; dock.
    assert message[start : start + 43] == bytes.fromhex(
        "0b0134120040ffff020080030c043b00050c000603040102070001080205ffff030c000901100e"
        "0a01020c"
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
    [(0, 0x4B), (4, 1), (6, 2), (14, 1)],
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


# Craft 7 as the C test's check_shown_faults sees it: cannon groups 0 and 1,
# launcher 0, turrets in slots 3 and 5; contacts 7, 9 and 12.
QUESTION_7 = Question(TEST_TICK, (CRAFT_7,), CONTACTS)


@pytest.mark.parametrize(
    ("actions", "match"),
    [
        (b"\x01\x00", "not one of the twelve"),
        (b"\x01\x0d", "not one of the twelve"),
        (b"\x02\x02\x01\x00\x02\x02\x00", "sent twice"),
        (b"\x01\x03\x0d", "out of range"),
        (b"\x01\x03\x05", "set by the engine alone"),
        (b"\x01\x03\x07", "set by the engine alone"),
        (b"\x01\x04\x00\x00", "out of range"),
        (b"\x02\x02\x01\x00", "ends inside a value"),
        (b"\x00\x00", "past its end"),
        (b"\x01\x05\x0b\x00", "target 11 is not a contact"),
        (b"\x01\x06\x04\x01\x00\x00", "out of range"),
        (b"\x01\x06\x01\x00\x00\x00", "out of range"),
        (b"\x01\x06\x00\x05\x00\x00", "out of range"),
        (b"\x01\x07\x02\x00", "out of range"),
        (b"\x01\x07\x00\x02", "out of range"),
        (b"\x01\x07\x01\x00", "launcher 1 is not shown ready"),
        (b"\x01\x08\x00", "out of range"),
        (b"\x01\x08\x01\x04\xff\xff", "slot 4 is not a turret"),
        (b"\x01\x08\x01\x10\xff\xff", "out of range"),
        (b"\x01\x08\x02\x03\xff\xff\x03\xff\xff", "out of range"),
        (b"\x01\x08\x01\x03\x0b\x00", "turret target 11 is not a contact"),
        (b"\x01\x08\x01\x03\x09", "ends inside a value"),
        (b"\x01\x09\x02\x00\x00", "out of range"),
        (b"\x01\x09\x01\x11\x0e", "out of range"),
        (b"\x01\x09\x01\x10", "ends inside a value"),
        (b"\x01\x0a\x02\x00", "order slot 2 is not one the view allows"),
        (b"\x01\x0a\x03\x00", "order slot 3 is not one the view allows"),
        (b"\x01\x0a\x04\x00", "out of range"),
        (b"\x01\x0a\x01\x01", "out of range"),
        (b"\x01\x0a\x01", "ends inside a value"),
        (b"\x01\x0b", "depart is not one the view's books allow"),
        (b"\x02\x0b\x0c", "depart and dock are both sent"),
    ],
    ids=[
        "kind 0", "kind 13", "twice", "activity 13", "activity 5", "activity 7", "interval 0", "short", "long",
        "target not shown", "link mode 4", "mode without burst", "burst without mode",
        "launcher 2", "paired 2", "launcher not counted", "no turrets",
        "not a turret", "slot 16", "slot twice", "turret target not shown",
        "turret cut off", "arriving 2", "speed 3601", "arrival cut off",
        "order to an empty slot", "order to slot 3", "order slot 4",
        "order state 1", "order cut off", "depart not allowed", "depart and dock",
    ],
)  # fmt: skip
def test_answer_breaking_the_engines_rules_is_refused(
    actions: bytes, match: str
) -> None:
    with pytest.raises(wire.WireError, match=match):
        wire.read_answer(answer_with_actions(actions), QUESTION_7)


def _actions_of(body: bytes, view: CraftView) -> Actions:
    return (
        wire.read_answer(body, Question(TEST_TICK, (view,), CONTACTS)).craft[0].actions
    )


def test_order_depart_and_dock_follow_the_views_books() -> None:
    """The C test's check_books_faults: an order to a slot with an order, to
    slot 3 with the skip due, none from a follower or after a skip; a depart
    only when the craft may jump, a dock only at its hangar."""
    order_1 = answer_with_actions(b"\x01\x0a\x01\x02")
    order_3 = answer_with_actions(b"\x01\x0a\x03\x00")
    depart = answer_with_actions(b"\x01\x0b")
    dock = answer_with_actions(b"\x01\x0c")
    assert _actions_of(order_1, CRAFT_7).order == OrderMove(1, 2)
    assert _actions_of(dock, CRAFT_7).dock
    skip_due = dataclasses.replace(CRAFT_7, books=Books.SKIP_DUE)
    assert _actions_of(order_3, skip_due).order == OrderMove(3)
    skipped = dataclasses.replace(CRAFT_7, order_slot=3, books=Books.SKIPPED)
    assert _actions_of(order_3, skipped).order == OrderMove(3)
    with pytest.raises(wire.WireError, match="order slot 1 is not one"):
        _actions_of(order_1, skipped)
    with pytest.raises(wire.WireError, match="follows a leader"):
        _actions_of(order_1, dataclasses.replace(CRAFT_7, leader=9))
    may_jump = dataclasses.replace(CRAFT_7, books=Books.MAY_JUMP)
    assert _actions_of(depart, may_jump).depart
    with pytest.raises(wire.WireError, match="dock is not one"):
        _actions_of(dock, may_jump)


def test_answer_firing_a_cannon_group_the_craft_lacks_is_refused() -> None:
    one_group = Question(TEST_TICK, (plain_view(7, cannon_group_count=1),), CONTACTS)
    body = answer_with_actions(b"\x01\x06\x00\x00\x01\x01")
    with pytest.raises(wire.WireError, match="cannon group 1"):
        wire.read_answer(body, one_group)
    assert wire.read_answer(body, QUESTION_7).craft[0].actions.lasers == (
        CannonGroup(),
        CannonGroup(LinkMode.ONE_AT_A_TIME, 1),
    )


def test_answer_firing_a_launcher_cooling_down_is_refused() -> None:
    cooling = Question(
        TEST_TICK,
        (plain_view(7, warhead_launcher_count=1, launcher_cooldown=(1, 0)),),
        CONTACTS,
    )
    with pytest.raises(wire.WireError, match="launcher 0 is not shown ready"):
        wire.read_answer(answer_with_actions(b"\x01\x07\x00\x01"), cooling)


def test_answer_naming_what_the_question_showed_is_read() -> None:
    body = answer_with_actions(
        b"\x04\x05\xff\xff\x06\x01\x01\x00\x00\x07\x00\x01\x08\x01\x03\x09\x00"
    )
    assert wire.read_answer(body, QUESTION_7).craft[0].actions == Actions(
        target=NO_OBJECT,
        lasers=(CannonGroup(LinkMode.ONE_AT_A_TIME, 1), CannonGroup()),
        warhead=Warhead(0, paired=True),
        turrets=(Turret(3, 9),),
    )


@pytest.mark.parametrize(
    ("cut", "match"),
    [(-1, "1 contacts"), (1, "1 contacts")],
    ids=["contact short", "contact long"],
)
def test_question_with_a_contact_of_the_wrong_length_is_refused(
    cut: int, match: str
) -> None:
    body = engine_question_bytes()[4:]
    body = body[:cut] if cut < 0 else body + b"\0"
    with pytest.raises(wire.WireError, match=match):
        wire.read_question(body)


def test_question_with_too_many_contacts_is_refused() -> None:
    contacts = tuple(plain_contact(index) for index in range(33))
    body = wire.question_bytes(Question(TEST_TICK, (), contacts))[4:]
    with pytest.raises(wire.WireError, match="33 contacts"):
        wire.read_question(body)


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

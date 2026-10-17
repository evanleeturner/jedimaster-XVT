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
from jedimaster_ai import world
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Answer
from jedimaster_ai.world import Arrival
from jedimaster_ai.world import Books
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import Commands
from jedimaster_ai.world import Contact
from jedimaster_ai.world import Countermeasure
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
    (0x41, 0x42, 0x43, 0x44), 0x4645, 0x47, 0x48, 0x49, 0x9291, 0xC2C14B4A, 0x4D4C,
    (0x51504F4E, 0x55545352, 0x59585756), (0x5D5C5B5A, 0x61605F5E, -6),
    (0x65646362, 0x69686766, 0x6D6C6B6A), 0x6E, 0x6F, 0x70, 0x71, 0x7372,
    0x7574, (0x7776, 0x7978, 0x7B7A, 0x7D7C), (0x7E, 0x7F, 0x80, 0x81),
    (0x82, 0x83, 0x84, 0x85), (0x13121110, 0x17161514, 0x1B1A1918, 0x1F1E1D1C),
    (0x23222120, 0x27262524, 0x2B2A2928, -7),
    (0x2F2E2D2C, 0x33323130, 0x37363534, 0x3B3A3938), 0x3C,
    (0x403F3E3D, 0x44434241, 0x48474645, 0x4C4B4A49, 0x504F4E4D, 0x54535251,
     0x58575655, 0x5C5B5A59),
    (0x605F5E5D, 0x64636261, 0x68676665, 0x6C6B6A69, 0x706F6E6D, 0x74737271,
     0x78777675, 0x7C7B7A79),
    (0x107F7E7D, 0x14131211, 0x18171615, 0x1C1B1A19, 0x201F1E1D, 0x24232221,
     0x28272625, -8), 0x29, 0x2B2A, 0x2D2C, (0x31302F2E, 0x35343332, -9),
    0x3736, 0x3938, 0x3A, 0x3B,
    maneuver_timer=0x3F3E3D3C,
    escorted=0x4241, station=(0x46454443, 0x4A494847, -10),
    start_point=(0x4E4D4C4B, 0x5251504F, -11), targets_now=0x07,
    static_target_mask=0xA09F9E9D9C9B9A99,
    board_point=(0x73727170, -13, 0x77767574), order_progress=(0xC0, 0xC1, 0xC2, 0xC3),
    boarded=(0xD1D0, 0xD3D2, 0xD5D4, 0xD7D6, 0xD9D8, 0xDBDA, 0xDDDC, 0xDFDE, 0xE1E0, 0xE3E2),
    board_phase=0xE4, lifetime_timer=0xE6E5, order_first_pair_mask=0xEAE9E8E7,
    drop_point=(-8, 0x6F6E6D6C, 0x7B7A7978), drop_phase=0xF1,
    notebook=bytes(0x40 + byte for byte in range(NOTEBOOK_BYTES)),
)  # fmt: skip
# contact_with_distinct_bytes in the C test.
DISTINCT_CONTACT = Contact(
    0x4241, 0x4443, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4E4D4C4B, 0x5251504F,
    -4, 0x5453, 0x5655, 0x5857, 0x5A59, 0x5C5B, -5, 0x605F5E5D, 0x64636261,
    0x68676665, 0x69, 0x6D6C6B6A, 0x6E, 0x7271706F, 0x7473,
    box_min=(-6, 0x78777675, 0x7C7B7A79), box_max=(-7, 0x7F7E7D7C, 0x7B7A7978),
    roll=0x8180, boarder=0x8382, weapon_inhibit_timer=0x8584, pulses_coming=0x86,
)  # fmt: skip
# static_with_distinct_bytes in the C test: the contact's bytes, a static's
# object index and flag.
DISTINCT_STATIC = dataclasses.replace(
    DISTINCT_CONTACT,
    object_index=0x7271,
    flags=0x08,
    weapon_inhibit_timer=0,
    pulses_coming=0,
)


def _c_bytes(text: str, name: str) -> bytes:
    block = re.search(rf"{name}\[[^\]]*\] = \{{(.*?)\}};", text, re.S)
    return bytes(int(value, 16) for value in block.group(1).split(","))


def engine_question_bytes() -> bytes:
    """Return the C test's expected question, its notebook bytes added."""
    text = C_TEST.read_text()
    head = _c_bytes(text, "expected")
    return (
        head
        + DISTINCT.notebook
        + _c_bytes(text, "expected_contacts")
        + _c_bytes(text, "expected_statics")
    )


DISTINCT_QUESTION = Question(
    TEST_TICK, (DISTINCT,), (DISTINCT_CONTACT,), (DISTINCT_STATIC,), 14
)


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
    del fields["boarded_count"]
    fields["boarded"] = ()
    fields.update(object_index=index, **changes)
    return CraftView(**fields, notebook=bytes(NOTEBOOK_BYTES))


def plain_contact(index: int) -> Contact:
    """Return a contact of object index, every other field 0."""
    fields = {
        name: (0,) * count if count > 1 else 0 for name, _, count in wire.CONTACT_LAYOUT
    }
    return Contact(**{**fields, "object_index": index})


# The C test's g_question: craft 7 with two cannon groups, launcher 0 ready,
# turrets in weapon slots 3 and 5 and a flare ready, leading, with orders in
# slots 0 and 1, at its hangar, told to evade; craft 9 with none of these;
# contacts 7, 9, 12.
CRAFT_7 = plain_view(
    7,
    turret_slot_mask=1 << 3 | 1 << 5,
    cannon_group_count=2,
    warhead_launcher_count=1,
    warheads_left=(2, 0),
    order_codes=(1, 2, 0, 0),
    leader=NO_OBJECT,
    books=Books.AT_HANGAR,
    countermeasure=Countermeasure.FLARE,
    countermeasures_left=3,
    countermeasure_working=1,
    commands=Commands.EVADE,
)
CONTACTS = tuple(plain_contact(index) for index in (7, 9, 12))
# Statics 40 and 41, which no contact is.
STATICS = tuple(plain_contact(index) for index in (40, 41))


def good_answer() -> tuple[Question, Answer]:
    """The C test's good answer: craft 7 sends every kind but depart, craft 9
    none."""
    question = Question(TEST_TICK, (CRAFT_7, plain_view(9)), CONTACTS, STATICS)
    every = Actions(
        Steer(0x1234, 0x4000, 0xFFFF),
        0x8000,
        Activity.DISABLED,
        59,
        target=12,
        lasers=(CannonGroup(LinkMode.ALL, 4), CannonGroup(LinkMode.ONE_AT_A_TIME, 2)),
        warhead=Warhead(0, paired=True),
        turrets=(Turret(5, NO_OBJECT, True), Turret(3, 12)),
        arrival=Arrival(True, 3600),
        order=OrderMove(1, 2),
        dock=True,
        countermeasure=True,
        evaded=True,
        maneuver_timer=0x7FFFFFFF,
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
    assert len(message) == wire.HEAD_BYTES + 118 + 3 + NOTEBOOK_BYTES
    assert wire.read_answer(message[4:], question) == answer


def test_answer_writes_actions_in_kind_order() -> None:
    _, answer = good_answer()
    message = wire.answer_bytes(answer)
    start = wire.HEAD_BYTES + 2 + NOTEBOOK_BYTES
    # Count 14; steer 0x1234 0x4000 0xFFFF; throttle 0x8000; activity 12;
    # think interval 59; target 12; lasers 3 4 1 2; warhead 0 paired;
    # turrets: slot 5 none, slot 3 at 12; arriving at 3,600; order slot 1
    # state 2; dock; countermeasure; evaded; maneuver timer 0x7FFFFFFF.
    # Turret slot 5 sets ion, slot 3 does not.
    expected = bytes.fromhex(
        "0e0134120040ffff020080030c043b00050c000603040102070001"
        "080205ffff01030c0000"
        "0901100e0a01020c0d0e0fffffff7f"
    )
    assert message[start : start + len(expected)] == expected


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
    [(0, 0x4B), (4, 1), (6, 2)],
    ids=["magic", "version", "kind"],
)
def test_question_with_a_bad_header_is_refused(offset: int, value: int) -> None:
    with pytest.raises(wire.WireError, match="header"):
        wire.read_question(changed_header(engine_question_bytes(), offset, value))


def test_question_header_ends_in_the_mission_version() -> None:
    body = changed_header(engine_question_bytes(), 14, 12)
    assert wire.read_question(body).mission_version == 12


@pytest.mark.parametrize("offset", [14, 15], ids=["low", "high"])
def test_answer_with_a_nonzero_header_u16_is_refused(offset: int) -> None:
    body = bytearray(answer_with_actions(b"\0"))
    body[offset] = 1
    with pytest.raises(wire.WireError, match="is not a version"):
        wire.read_answer(bytes(body), QUESTION_7)


def test_question_shorter_than_its_header_is_refused() -> None:
    with pytest.raises(wire.WireError, match="shorter than its header"):
        wire.read_question(engine_question_bytes()[4:10])


@pytest.mark.parametrize("cut", [-1, 1], ids=["short", "long"])
def test_question_of_the_wrong_length_is_refused(cut: int) -> None:
    body = engine_question_bytes()[4:]
    body = body[:cut] if cut < 0 else body + b"\0"
    with pytest.raises(wire.WireError, match="1 statics"):
        wire.read_question(body)


def test_question_cut_inside_its_contacts_is_refused() -> None:
    body = engine_question_bytes()[4:]
    cut = wire.HEADER.size + wire.CRAFT_VIEW_BYTES + wire.CONTACT_HEAD.size + 10
    with pytest.raises(wire.WireError, match="1 contacts"):
        wire.read_question(body[:cut])


def answer_with_actions(actions: bytes) -> bytes:
    """Return an answer about craft 7 alone, with these action bytes."""
    head = wire.HEADER.pack(wire.MAGIC, wire.VERSION, wire.KIND_ANSWER, TEST_TICK, 1, 0)
    return head + struct.pack("<H", 7) + bytes(NOTEBOOK_BYTES) + actions


# Craft 7 as the C test's check_shown_faults sees it: cannon groups 0 and 1,
# launcher 0, turrets in slots 3 and 5; contacts 7, 9 and 12.
QUESTION_7 = Question(TEST_TICK, (CRAFT_7,), CONTACTS, STATICS)


@pytest.mark.parametrize(
    ("actions", "match"),
    [
        (b"\x01\x00", "not one of the eighteen"),
        (b"\x01\x13", "not one of the eighteen"),
        (b"\x02\x02\x01\x00\x02\x02\x00", "sent twice"),
        (b"\x01\x03\x13", "out of range"),
        (b"\x01\x03\x05", "set by the engine alone"),
        (b"\x01\x03\x07", "set by the engine alone"),
        (b"\x01\x04\x00\x00", "out of range"),
        (b"\x02\x02\x01\x00", "ends inside a value"),
        (b"\x00\x00", "past its end"),
        (b"\x01\x05\x0b\x00", "target 11 is not a contact"),
        (b"\x01\x05\x2a\x00", "target 42 is not a contact or a static"),
        (b"\x01\x08\x01\x03\x28\x00\x00", "turret target 40 is not a contact"),
        (b"\x01\x06\x04\x01\x00\x00", "out of range"),
        (b"\x01\x06\x01\x00\x00\x00", "out of range"),
        (b"\x01\x06\x00\x05\x00\x00", "out of range"),
        (b"\x01\x07\x02\x00", "out of range"),
        (b"\x01\x07\x00\x02", "out of range"),
        (b"\x01\x07\x01\x00", "launcher 1 is not shown ready"),
        (b"\x01\x08\x00", "out of range"),
        (b"\x01\x08\x01\x04\xff\xff\x00", "slot 4 is not a turret"),
        (b"\x01\x08\x01\x10\xff\xff\x00", "out of range"),
        (b"\x01\x08\x02\x03\xff\xff\x00\x03\xff\xff\x00", "out of range"),
        (b"\x01\x08\x01\x03\xff\xff\x02", "ion is 2"),
        (b"\x01\x0f\x00\x00\x00\x80", "out of range"),
        (b"\x01\x08\x01\x03\x0b\x00\x00", "turret target 11 is not a contact"),
        (b"\x01\x08\x01\x03\x09", "ends inside a value"),
        (b"\x01\x0f\x00\x00\x00", "ends inside a value"),
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
        (b"\x01\x10\x00", "out of range"),
        (b"\x01\x10\x38", "out of range"),
        (b"\x01\x10", "ends inside a value"),
    ],
    ids=[
        "kind 0", "kind 19", "twice", "activity 19", "activity 5", "activity 7", "interval 0", "short", "long",
        "target not shown", "target past the statics", "turret aimed at a static",
        "link mode 4", "mode without burst", "burst without mode",
        "launcher 2", "paired 2", "launcher not counted", "no turrets",
        "not a turret", "slot 16", "slot twice", "ion 2", "timer past the limit",
        "turret target not shown", "turret cut off", "timer cut off", "arriving 2", "speed 3601", "arrival cut off",
        "order to an empty slot", "order to slot 3", "order slot 4",
        "order state 1", "order cut off", "depart not allowed", "depart and dock",
        "self destruct 0", "self destruct 56", "self destruct cut off",
    ],
)  # fmt: skip
def test_answer_breaking_the_engines_rules_is_refused(
    actions: bytes, match: str
) -> None:
    with pytest.raises(wire.WireError, match=match):
        wire.read_answer(answer_with_actions(actions), QUESTION_7)


def _actions_of(body: bytes, view: CraftView) -> Actions:
    return (
        wire.read_answer(body, Question(TEST_TICK, (view,), CONTACTS, STATICS))
        .craft[0]
        .actions
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


@pytest.mark.parametrize(
    ("changes", "ready"),
    [
        ({}, True),
        ({"countermeasure_cooldown": 1}, False),
        ({"countermeasures_left": 0}, False),
        ({"countermeasure_working": 0}, False),
        ({"chaff_seconds": 4}, True),
        ({"countermeasure": Countermeasure.CHAFF, "chaff_seconds": 4}, False),
        ({"countermeasure": Countermeasure.CHAFF, "countermeasure_cooldown": 1}, True),
        ({"countermeasure": Countermeasure.NONE}, False),
        ({"countermeasure": 3}, False),
    ],
    ids=[
        "flare",
        "flare cooling",
        "none left",
        "not working",
        "flare with chaff seconds",
        "chaff running",
        "chaff with a cooldown",
        "no type",
        "cluster mine",
    ],
)
def test_countermeasure_follows_what_the_view_shows(
    changes: dict[str, int], ready: bool
) -> None:
    """The C test's check_countermeasure_faults: chaff only with no chaff
    seconds left, a flare only with its cooldown at 0, either working with
    one left."""
    launch = answer_with_actions(b"\x01\x0d")
    shown = dataclasses.replace(CRAFT_7, **changes)
    assert shown.countermeasure_ready == ready
    if ready:
        assert _actions_of(launch, shown).countermeasure
    else:
        with pytest.raises(wire.WireError, match="countermeasure is not shown"):
            _actions_of(launch, shown)


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
        (
            plain_view(
                7,
                warhead_launcher_count=1,
                warheads_left=(1, 0),
                launcher_cooldown=(1, 0),
            ),
        ),
        CONTACTS,
    )
    with pytest.raises(wire.WireError, match="launcher 0 is not shown ready"):
        wire.read_answer(answer_with_actions(b"\x01\x07\x00\x01"), cooling)


def test_answer_firing_an_empty_launcher_is_refused() -> None:
    # The engine would spend 472 ticks of cooldown and fly nothing.
    empty = Question(
        TEST_TICK,
        (plain_view(7, warhead_launcher_count=2, warheads_left=(0, 3)),),
        CONTACTS,
    )
    with pytest.raises(wire.WireError, match="launcher 0 is not shown ready"):
        wire.read_answer(answer_with_actions(b"\x01\x07\x00\x01"), empty)


def test_answer_naming_what_the_question_showed_is_read() -> None:
    body = answer_with_actions(
        b"\x05\x05\xff\xff\x06\x01\x01\x00\x00\x07\x00\x01\x08\x01\x03\x09\x00\x01"
        b"\x0f\xff\xff\xff\x7f"
    )
    assert wire.read_answer(body, QUESTION_7).craft[0].actions == Actions(
        target=NO_OBJECT,
        lasers=(CannonGroup(LinkMode.ONE_AT_A_TIME, 1), CannonGroup()),
        warhead=Warhead(0, paired=True),
        turrets=(Turret(3, 9, True),),
        maneuver_timer=0x7FFFFFFF,
    )


@pytest.mark.parametrize(
    ("cut", "match"),
    [(-1, "1 statics"), (1, "1 statics")],
    ids=["static short", "static long"],
)
def test_question_with_a_static_of_the_wrong_length_is_refused(
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


@pytest.mark.parametrize(
    ("commands", "allowed"),
    [
        (Commands.EVADE, True),
        (Commands.WAIT | Commands.EVADE, True),
        (Commands.WAIT, False),
        (Commands(0), False),
    ],
    ids=["evade", "wait and evade", "wait", "none"],
)
def test_evaded_follows_the_view_commands(commands: Commands, allowed: bool) -> None:
    """The C test's check_evaded_faults: an evaded only while the view shows
    the evade command."""
    evaded = answer_with_actions(b"\x01\x0e")
    shown = dataclasses.replace(CRAFT_7, commands=commands)
    if allowed:
        assert _actions_of(evaded, shown).evaded
    else:
        with pytest.raises(wire.WireError, match="no evade command"):
            _actions_of(evaded, shown)


def test_a_question_carries_the_timer_and_the_shooter(view, contact) -> None:
    question = Question(
        1, (view(maneuver_timer=1180),), (contact(hit_by=9), contact(hit_by=NO_OBJECT))
    )
    read = wire.read_question(wire.question_bytes(question)[wire.LENGTH.size :])
    assert read.craft[0].maneuver_timer == 1180
    assert [found.hit_by for found in read.contacts] == [9, NO_OBJECT]


def test_a_question_carries_the_model_box_and_the_roll(contact) -> None:
    boxed = contact(
        box_min=(-3000, -162468, -7), box_max=(9000, 165215, 12), roll=0xC000
    )
    question = Question(1, (), (boxed,), (dataclasses.replace(boxed, roll=0x2000),))
    message = wire.question_bytes(question)
    assert len(message) == 4 + wire.HEADER.size + 2 * (4 + 91)
    read = wire.read_question(message[wire.LENGTH.size :])
    assert read == question
    assert read.contacts[0].box_min == (-3000, -162468, -7)
    assert read.contacts[0].box_max == (9000, 165215, 12)
    assert (read.contacts[0].roll, read.statics[0].roll) == (0xC000, 0x2000)


@pytest.mark.parametrize(
    ("timer", "pulses", "coming"), [(65535, 2, 3), (300, 255, 255), (0, 0, 0)]
)
def test_a_question_carries_the_timer_the_pulses_and_the_version(
    contact, timer: int, pulses: int, coming: int
) -> None:
    hit = contact(
        weapon_inhibit_timer=timer, pulses_coming=pulses, warheads_coming=coming
    )
    question = Question(1, (), (hit,), (), 14)
    message = wire.question_bytes(question)
    assert len(message) == 4 + wire.HEADER.size + 2 * 4 + 91
    read = wire.read_question(message[wire.LENGTH.size :])
    assert read == question
    assert read.contacts[0].weapon_inhibit_timer == timer
    assert read.contacts[0].pulses_coming == pulses
    assert read.mission_version == 14


def test_a_question_reads_what_wire_14_adds_as_absent(view, contact) -> None:
    question = Question(1, (view(),), (contact(),))
    read = wire.read_question(wire.question_bytes(question)[wire.LENGTH.size :])
    (craft,) = read.craft
    assert (craft.escorted, craft.station) == (NO_OBJECT, (0, 0, 0))
    assert craft.start_point == (0, 0, 0)


def test_the_maneuver_timer_action_is_written() -> None:
    actions = Actions(maneuver_timer=1180)
    answer = Answer(1, (CraftAnswer(7, bytes(NOTEBOOK_BYTES), actions),))
    message = wire.answer_bytes(answer)
    assert message[-6:] == bytes.fromhex("010f9c040000")


def test_the_ion_switch_is_written_and_read() -> None:
    actions = Actions(turrets=(Turret(3, 9, True), Turret(5, 9, False)))
    answer = Answer(TEST_TICK, (CraftAnswer(7, bytes(NOTEBOOK_BYTES), actions),))
    message = wire.answer_bytes(answer)
    assert message[-11:] == bytes.fromhex("0108020309000105090000")
    assert wire.read_answer(message[4:], QUESTION_7) == answer


def test_answer_may_target_a_listed_static() -> None:
    """The C test's target_static_40 and _41: a static the question listed is
    a target; a turret still takes contacts only."""
    for index in (40, 41):
        body = answer_with_actions(b"\x01\x05" + bytes([index, 0]))
        assert _actions_of(body, CRAFT_7).target == index


def test_statics_are_read_and_written_after_the_contacts() -> None:
    question = Question(TEST_TICK, (plain_view(7),), CONTACTS, STATICS)
    message = wire.question_bytes(question)
    assert wire.read_question(message[4:]) == question
    assert message[wire.HEAD_BYTES + wire.CRAFT_VIEW_BYTES :][
        wire.CONTACT_HEAD.size + 3 * wire.CONTACT_FIELDS.size :
    ][:4] == struct.pack("<HH", 2, 0)


def test_the_most_statics_is_64() -> None:
    many = tuple(plain_contact(index) for index in range(64))
    question = Question(TEST_TICK, (), (), many)
    assert wire.read_question(wire.question_bytes(question)[4:]) == question
    too_many = Question(TEST_TICK, (), (), many + (plain_contact(64),))
    with pytest.raises(wire.WireError, match="statics"):
        wire.read_question(wire.question_bytes(too_many)[4:])


def test_a_static_count_with_a_nonzero_pad_is_refused() -> None:
    body = bytearray(wire.question_bytes(Question(TEST_TICK, (), (), STATICS))[4:])
    body[wire.HEADER.size + wire.CONTACT_HEAD.size + 2] = 1
    with pytest.raises(wire.WireError, match="statics"):
        wire.read_question(bytes(body))


@pytest.mark.parametrize("units", [1, 55])
def test_self_destruct_is_read_and_written(units: int) -> None:
    body = answer_with_actions(bytes([1, 0x10, units]))
    answer = wire.read_answer(body, QUESTION_7)
    assert answer.craft[0].actions == Actions(self_destruct=units)
    assert wire.answer_bytes(answer)[4:] == body


@pytest.mark.parametrize("units", [0, 56, 255])
def test_self_destruct_outside_its_units_is_not_made(units: int) -> None:
    with pytest.raises(ValueError, match="self_destruct"):
        Actions(self_destruct=units)


def test_boarded_is_padded_with_zeros_and_cut_to_its_count() -> None:
    view = dataclasses.replace(CRAFT_7, boarded=(0x301, 0x302, 0x303))
    question = Question(TEST_TICK, (view,), CONTACTS, STATICS)
    message = wire.question_bytes(question)
    assert wire.read_question(message[4:]) == question
    # u8 count then ten u16, the last seven 0, then the 20 bytes of the dock
    # phase, lifetime timer, first pair mask, drop point and drop phase: 41
    # bytes before the notebook.
    tail = message[4 + wire.HEADER.size :][
        wire.VIEW_FIELDS.size - 41 : wire.VIEW_FIELDS.size - 20
    ]
    assert tail == bytes([3]) + struct.pack("<10H", 0x301, 0x302, 0x303, *[0] * 7)


def test_a_view_that_boarded_too_many_is_not_written_or_read() -> None:
    view = dataclasses.replace(CRAFT_7, boarded=tuple(range(11)))
    with pytest.raises(wire.WireError, match="at most 10"):
        wire.question_bytes(Question(TEST_TICK, (view,), CONTACTS, STATICS))
    message = bytearray(wire.question_bytes(QUESTION_7))
    count_at = 4 + wire.HEADER.size + wire.VIEW_FIELDS.size - 41
    message[count_at] = 11
    with pytest.raises(wire.WireError, match="at most 10"):
        wire.read_question(bytes(message[4:]))


# Craft 7 as a boarder: at its board point on Give, not docking; contacts 7,
# 9, 12. The C test's check_board_faults.
BOARDER = dataclasses.replace(
    CRAFT_7, order_code=12, books=Books.BOARD_POINT | Books.AT_BOARD_POINT
)


def _board_question(view: CraftView) -> Question:
    return Question(TEST_TICK, (view,), CONTACTS, STATICS)


def _read_board(actions: bytes, view: CraftView) -> Actions:
    body = answer_with_actions(actions)
    return wire.read_answer(body, _board_question(view)).craft[0].actions


def test_board_is_read_and_written() -> None:
    body = answer_with_actions(b"\x01\x11\x0c\x00")
    answer = wire.read_answer(body, _board_question(BOARDER))
    assert answer.craft[0].actions == Actions(board=12)
    assert wire.answer_bytes(answer)[4:] == body
    stop = dataclasses.replace(BOARDER, board_phase=2)
    body = answer_with_actions(b"\x01\x11\xff\xff")
    answer = wire.read_answer(body, _board_question(stop))
    assert answer.craft[0].actions == Actions(board=NO_OBJECT)
    assert wire.answer_bytes(answer)[4:] == body


@pytest.mark.parametrize("code", [12, 13, 14, 15, 16, 17, 31, 32])
def test_every_boarding_order_may_board(code: int) -> None:
    view = dataclasses.replace(BOARDER, order_code=code)
    assert _read_board(b"\x01\x11\x0c\x00", view).board == 12


@pytest.mark.parametrize("code", [0, 1, 11, 18, 30, 33, 255])
def test_no_other_order_may_board(code: int) -> None:
    view = dataclasses.replace(BOARDER, order_code=code)
    with pytest.raises(wire.WireError, match="not one the view allows"):
        _read_board(b"\x01\x11\x0c\x00", view)


@pytest.mark.parametrize(
    ("changes", "actions", "match"),
    [
        ({"books": Books.BOARD_POINT}, b"\x01\x11\x0c\x00", "not one the view allows"),
        ({"board_phase": 1}, b"\x01\x11\x0c\x00", "not one the view allows"),
        ({}, b"\x01\x11\x0b\x00", "not one the view allows"),
        ({}, b"\x01\x11\x28\x00", "not one the view allows"),
        ({}, b"\x02\x11\x0c\x00\x05\x09\x00", "not the answer's target"),
        ({}, b"\x01\x11\xff\xff", "stops a dock only in phase 1 or 2"),
        ({"board_phase": 3}, b"\x01\x11\xff\xff", "stops a dock only in phase 1 or 2"),
        ({"board_phase": 1}, b"\x01\x0f\x05\x00\x00\x00", "timer is refused"),
        ({"board_phase": 3}, b"\x01\x0f\x05\x00\x00\x00", "timer is refused"),
        ({"board_phase": 1}, b"\x01\x05\x09\x00", "target is refused"),
        ({"board_phase": 3}, b"\x01\x05\x09\x00", "target is refused"),
    ],
    ids=[
        "not at the point", "already docking", "not a contact", "a static",
        "another target in the answer", "stop in phase 0", "stop in phase 3",
        "timer in phase 1", "timer in phase 3", "target in phase 1",
        "target in phase 3",
    ],
)  # fmt: skip
def test_board_the_view_does_not_allow_is_refused(
    changes: dict, actions: bytes, match: str
) -> None:
    with pytest.raises(wire.WireError, match=match):
        _read_board(actions, dataclasses.replace(BOARDER, **changes))


def test_board_stops_a_dock_in_phases_one_and_two_only() -> None:
    for phase in (1, 2):
        view = dataclasses.replace(BOARDER, board_phase=phase)
        assert _read_board(b"\x01\x11\xff\xff", view).board == NO_OBJECT
        # A target in the same answer is refused with the rest: the engine
        # flies the craft.
        both = b"\x02\x11\xff\xff\x05\x09\x00"
        with pytest.raises(wire.WireError, match="target is refused"):
            _read_board(both, view)


def test_board_names_the_answers_own_target_in_either_order() -> None:
    first = _read_board(b"\x02\x11\x0c\x00\x05\x0c\x00", BOARDER)
    second = _read_board(b"\x02\x05\x0c\x00\x11\x0c\x00", BOARDER)
    assert first == second == Actions(target=12, board=12)


def test_the_maneuver_timer_is_refused_before_the_dock_not_during_it() -> None:
    timer = b"\x01\x0f\x05\x00\x00\x00"
    assert _read_board(timer, BOARDER).maneuver_timer == 5


@pytest.mark.parametrize("phase", [1, 2, 3])
def test_steer_and_throttle_are_read_while_the_craft_docks(phase: int) -> None:
    view = dataclasses.replace(BOARDER, board_phase=phase)
    body = b"\x02\x01\x01\x00\x02\x00\x03\x00\x02\x04\x00"
    actions = _read_board(body, view)
    assert actions.steer == Steer(1, 2, 3) and actions.throttle == 4


def test_board_is_never_sent_with_a_depart_or_a_dock() -> None:
    with pytest.raises(ValueError, match="board"):
        Actions(board=12, depart=True)
    with pytest.raises(ValueError, match="board"):
        Actions(board=NO_OBJECT, dock=True)


@pytest.mark.parametrize("object_index", [-1, 0x10000])
def test_board_outside_sixteen_bits_is_not_made(object_index: int) -> None:
    with pytest.raises(ValueError, match="board"):
        Actions(board=object_index)


# Craft 7 as a Drop Off carrier: at its drop point, nothing running. The C
# test's check_drop_off_faults.
CARRIER = dataclasses.replace(
    CRAFT_7, order_code=18, books=Books.DROP_POINT | Books.AT_DROP_POINT
)


def _read_drop(actions: bytes, view: CraftView) -> Actions:
    return (
        wire.read_answer(answer_with_actions(actions), _board_question(view))
        .craft[0]
        .actions
    )


def test_drop_off_is_read_and_written() -> None:
    body = answer_with_actions(b"\x01\x12\x01")
    answer = wire.read_answer(body, _board_question(CARRIER))
    assert answer.craft[0].actions == Actions(drop_off=1)
    assert wire.answer_bytes(answer)[4:] == body
    running = dataclasses.replace(CARRIER, drop_phase=1)
    body = answer_with_actions(b"\x01\x12\x00")
    answer = wire.read_answer(body, _board_question(running))
    assert answer.craft[0].actions == Actions(drop_off=0)
    assert wire.answer_bytes(answer)[4:] == body


def test_drop_off_the_view_does_not_allow_is_refused() -> None:
    start, stop = b"\x01\x12\x01", b"\x01\x12\x00"
    # No AT_DROP_POINT, DROP_POINT alone, a drop or a dock running.
    for changes in (
        {"books": Books.DROP_POINT},
        {"books": Books(0)},
        {"drop_phase": 1},
        {"drop_phase": 2},
        {"board_phase": 2},
    ):
        with pytest.raises(wire.WireError, match="starts a drop only"):
            _read_drop(start, dataclasses.replace(CARRIER, **changes))
    # A stop with none running.
    with pytest.raises(wire.WireError, match="stops a drop only"):
        _read_drop(stop, CARRIER)
    # Any value but 0 and 1 (the engine refuses it too).
    running = dataclasses.replace(CARRIER, drop_phase=1)
    with pytest.raises(wire.WireError, match="not 0 or 1"):
        _read_drop(b"\x01\x12\x02", running)


@pytest.mark.parametrize("phase", [1, 2])
def test_drop_off_stops_a_drop_in_either_phase(phase: int) -> None:
    view = dataclasses.replace(CARRIER, drop_phase=phase)
    assert _read_drop(b"\x01\x12\x00", view).drop_off == 0


@pytest.mark.parametrize(
    "others", [b"\x0b", b"\x0a", b"\x11\x0c\x00"], ids=["dock", "depart", "board"]
)
def test_drop_off_never_shares_an_answer_with_a_leaving_or_a_board(
    others: bytes,
) -> None:
    view = dataclasses.replace(
        CARRIER, books=CARRIER.books | Books.AT_HANGAR | Books.MAY_JUMP
    )
    body = b"\x02\x12\x01" + others
    with pytest.raises(wire.WireError):
        _read_drop(body, view)
    body = b"\x02" + others + b"\x12\x01"
    with pytest.raises(wire.WireError):
        _read_drop(body, view)


@pytest.mark.parametrize("phase", [1, 2])
def test_the_timer_and_a_target_are_refused_while_the_craft_drops(phase: int) -> None:
    view = dataclasses.replace(CARRIER, drop_phase=phase)
    with pytest.raises(wire.WireError, match="timer is refused"):
        _read_drop(b"\x01\x0f\x05\x00\x00\x00", view)
    for target in (b"\x01\x05\x09\x00", b"\x01\x05\xff\xff"):
        with pytest.raises(wire.WireError, match="target is refused"):
            _read_drop(target, view)
    # Steer, throttle and an order move are read.
    body = b"\x02\x01\x01\x00\x02\x00\x03\x00\x02\x04\x00"
    actions = _read_drop(body, view)
    assert actions.steer == Steer(1, 2, 3) and actions.throttle == 4


def test_the_timer_and_a_target_are_read_when_nothing_runs() -> None:
    assert _read_drop(b"\x01\x0f\x05\x00\x00\x00", CARRIER).maneuver_timer == 5
    assert _read_drop(b"\x01\x05\x09\x00", CARRIER).target == 9


def test_engine_flies_when_a_dock_or_a_drop_runs() -> None:
    assert not world.engine_flies(CARRIER)
    assert world.engine_flies(dataclasses.replace(CARRIER, board_phase=3))
    assert world.engine_flies(dataclasses.replace(CARRIER, drop_phase=2))


def test_drop_off_is_never_sent_with_a_depart_a_dock_or_a_board() -> None:
    for others in ({"depart": True}, {"dock": True}, {"board": 12}):
        with pytest.raises(ValueError, match="drop_off"):
            Actions(drop_off=1, **others)


@pytest.mark.parametrize("value", [-1, 2, 255])
def test_drop_off_is_one_or_zero(value: int) -> None:
    with pytest.raises(ValueError, match="drop_off"):
        Actions(drop_off=value)

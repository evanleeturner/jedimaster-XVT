"""The bytes of the messages between the engine and the AI program.

Purpose:
    Turn the engine's questions into ``world`` classes and the program's
    answers into bytes, exactly as ``engine/src/xvt_runtime/ai/ai_wire.h``
    sets them out. The engine's half, writing questions and reading answers
    by the engine's rules, is here too, for tests and replays.

Flow:
    1. ``read_message`` takes one message off a stream: a u32 length, then
       that many bytes
    2. ``read_question`` checks the header and the length its craft and
       contact counts need, then reads each craft view and each contact
    3. ``answer_bytes`` writes an answer, its length field first; actions go
       in kind order

Invariants:
    - Every value is little-endian on every machine.
    - A message that breaks the format raises WireError; nothing is guessed.
    - ``read_answer`` refuses what the engine's reader refuses: a wrong
      header, tick, count or craft, an unknown or repeated action kind, an
      activity past the last or one the engine alone sets, a think interval of 0, an arrival's arriving
      past 1 or speed past 3,600, a short or long message, a target, cannon group, launcher or turret the question
      did not show, an order from a follower or to a slot the view does not
      allow, a depart or dock the view's books do not show, or both, a
      countermeasure the view does not show ready, an evaded with no
      evade command shown, a turret ion past 1, and a maneuver timer past
      0x7FFFFFFF.
"""

from __future__ import annotations

import enum
import struct
import typing as t

from .world import Actions
from .world import Answer
from .world import Arrival
from .world import Books
from .world import CannonGroup
from .world import Commands
from .world import Contact
from .world import CONTACTS_MAX
from .world import CraftAnswer
from .world import CraftView
from .world import NO_OBJECT
from .world import NOTEBOOK_BYTES
from .world import ORDER_SLOTS
from .world import OrderMove
from .world import Question
from .world import STATICS_MAX
from .world import Steer
from .world import Turret
from .world import Warhead

MAGIC = 0x57504D4A  # "JMPW"
VERSION = 15
KIND_QUESTION = 1
KIND_ANSWER = 2
LENGTH = struct.Struct("<I")
# After the length field: magic, version, kind, tick, craft count, zero.
HEADER = struct.Struct("<IHHIHH")
HEAD_BYTES = LENGTH.size + HEADER.size
# A craft view's fields in the engine's order, each with its struct code and
# count (2 for a pair); the notebook follows them.
VIEW_LAYOUT = (
    ("object_index", "H", 1),
    ("object_signature", "H", 1),
    ("object_type", "B", 1),
    ("flight_group", "B", 1),
    ("order_code", "B", 1),
    ("order_slot", "B", 1),
    ("activity", "B", 1),
    ("team", "B", 1),
    ("skill", "H", 1),
    ("think_interval", "H", 1),
    ("throttle", "H", 1),
    ("x", "i", 1),
    ("y", "i", 1),
    ("z", "i", 1),
    ("yaw", "H", 1),
    ("pitch", "H", 1),
    ("roll", "H", 1),
    ("speed", "H", 1),
    ("random_seed", "h", 1),
    ("order_target_mask", "I", 1),
    ("turret_slot_mask", "H", 1),
    ("cannon_group_count", "B", 1),
    ("warhead_launcher_count", "B", 1),
    ("cannon_type", "B", 2),
    ("warhead_type", "B", 2),
    ("warheads_left", "B", 2),
    ("launcher_cooldown", "H", 2),
    ("enemy_mask", "I", 1),
    ("leader", "H", 1),
    ("order_codes", "B", 4),
    ("order_states", "B", 4),
    ("order_throttle", "H", 1),
    ("order_variable1", "B", 1),
    ("order_variable2", "B", 1),
    ("order_speed", "B", 1),
    ("full_speed", "H", 1),
    ("books", "H", 1),
    ("mothership", "H", 1),
    ("outside_hangar", "i", 3),
    ("inside_hangar", "i", 3),
    ("home_point", "i", 3),
    ("countermeasure", "B", 1),
    ("countermeasures_left", "B", 1),
    ("countermeasure_working", "B", 1),
    ("threat_count", "B", 1),
    ("chaff_seconds", "H", 1),
    ("countermeasure_cooldown", "H", 1),
    ("threat_object", "H", 4),
    ("threat_type", "B", 4),
    ("threat_decoyed", "B", 4),
    ("threat_x", "i", 4),
    ("threat_y", "i", 4),
    ("threat_z", "i", 4),
    ("waypoint_mask", "B", 1),
    ("waypoint_x", "i", 8),
    ("waypoint_y", "i", 8),
    ("waypoint_z", "i", 8),
    ("commands", "B", 1),
    ("command_target", "H", 1),
    ("avoid_target", "H", 1),
    ("place", "i", 3),
    ("lock_ticks", "H", 1),
    ("lock_needed", "H", 1),
    ("lock_state", "B", 1),
    ("skill_level", "B", 1),
    ("maneuver_timer", "I", 1),
    ("escorted", "H", 1),
    ("station", "i", 3),
    ("start_point", "i", 3),
    ("targets_now", "B", 1),
    ("static_target_mask", "Q", 1),
)
VIEW_FIELDS = struct.Struct(
    "<" + "".join(code * count for _, code, count in VIEW_LAYOUT)
)
CRAFT_VIEW_BYTES = VIEW_FIELDS.size + NOTEBOOK_BYTES
# The contact count and its zero.
CONTACT_HEAD = struct.Struct("<HH")
CONTACT_LAYOUT = (
    ("object_index", "H", 1),
    ("object_signature", "H", 1),
    ("object_type", "B", 1),
    ("genus", "B", 1),
    ("team", "B", 1),
    ("flight_group", "B", 1),
    ("object_kind", "B", 1),
    ("flags", "B", 1),
    ("x", "i", 1),
    ("y", "i", 1),
    ("z", "i", 1),
    ("yaw", "H", 1),
    ("pitch", "H", 1),
    ("speed", "H", 1),
    ("target", "H", 1),
    ("last_attacker", "H", 1),
    ("front_shield", "i", 1),
    ("rear_shield", "i", 1),
    ("hull_damage", "I", 1),
    ("hull_strength", "I", 1),
    ("activity", "B", 1),
    ("size", "I", 1),
    ("warheads_coming", "B", 1),
    ("warheads_coming_damage", "I", 1),
    ("hit_by", "H", 1),
    ("box_min", "i", 3),
    ("box_max", "i", 3),
    ("roll", "H", 1),
)
CONTACT_FIELDS = struct.Struct(
    "<" + "".join(code * count for _, code, count in CONTACT_LAYOUT)
)
U8 = struct.Struct("<B")
U16 = struct.Struct("<H")
STEER = struct.Struct("<HHH")
LASERS = struct.Struct("<BBBB")
WARHEAD = struct.Struct("<BB")
TURRET = struct.Struct("<BHB")
U32 = struct.Struct("<I")
ARRIVAL = struct.Struct("<BH")
ORDER = struct.Struct("<BB")


class ActionKind(enum.IntEnum):
    """The action kinds of a craft answer, in the order they are written."""

    STEER = 1
    THROTTLE = 2
    ACTIVITY = 3
    THINK_INTERVAL = 4
    TARGET = 5
    LASERS = 6
    WARHEAD = 7
    TURRETS = 8
    ARRIVAL = 9
    ORDER = 10
    DEPART = 11
    DOCK = 12
    COUNTERMEASURE = 13
    EVADED = 14
    MANEUVER_TIMER = 15


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


def _fields_from(
    layout: tuple[tuple[str, str, int], ...], values: tuple[int, ...]
) -> dict[str, t.Any]:
    """Return the layout's fields from its flat values; a count over 1 is a tuple."""
    fields: dict[str, t.Any] = {}
    at = 0
    for name, _, count in layout:
        fields[name] = values[at] if count == 1 else tuple(values[at : at + count])
        at += count
    return fields


def _values_of(layout: tuple[tuple[str, str, int], ...], record: t.Any) -> list[int]:
    """Return the record's fields as the layout's flat values."""
    values: list[int] = []
    for name, _, count in layout:
        value = getattr(record, name)
        values.extend(value if count > 1 else (value,))
    return values


def _view_from(values: tuple[int, ...], notebook: bytes) -> CraftView:
    return CraftView(**_fields_from(VIEW_LAYOUT, values), notebook=notebook)


def _view_values(view: CraftView) -> list[int]:
    return _values_of(VIEW_LAYOUT, view)


def _contact_from(values: tuple[int, ...]) -> Contact:
    return Contact(**_fields_from(CONTACT_LAYOUT, values))


def read_question(body: bytes) -> Question:
    """Return the question these bytes, after the length field, hold."""
    tick, count = _read_header(body, KIND_QUESTION)
    views_end = HEADER.size + count * CRAFT_VIEW_BYTES
    if len(body) < views_end + CONTACT_HEAD.size:
        raise WireError(
            f"a question about {count} craft holds {len(body)} bytes, "
            f"too few for its views and contact count"
        )
    contact_count, zero = CONTACT_HEAD.unpack_from(body, views_end)
    contacts_at = views_end + CONTACT_HEAD.size
    contacts_end = contacts_at + contact_count * CONTACT_FIELDS.size
    if (
        zero != 0
        or contact_count > CONTACTS_MAX
        or len(body) < contacts_end + CONTACT_HEAD.size
    ):
        raise WireError(
            f"a question about {count} craft and {contact_count} contacts "
            f"(zero={zero}) holds {len(body)} bytes, too few or too many"
        )
    static_count, static_zero = CONTACT_HEAD.unpack_from(body, contacts_end)
    statics_at = contacts_end + CONTACT_HEAD.size
    expected = statics_at + static_count * CONTACT_FIELDS.size
    if static_zero != 0 or static_count > STATICS_MAX or len(body) != expected:
        raise WireError(
            f"a question with {static_count} statics (zero={static_zero}) "
            f"holds {len(body)} bytes, not {expected}"
        )
    craft = []
    for offset in range(HEADER.size, views_end, CRAFT_VIEW_BYTES):
        notebook = body[offset + VIEW_FIELDS.size : offset + CRAFT_VIEW_BYTES]
        craft.append(_view_from(VIEW_FIELDS.unpack_from(body, offset), notebook))
    contacts = tuple(
        _contact_from(CONTACT_FIELDS.unpack_from(body, offset))
        for offset in range(contacts_at, contacts_end, CONTACT_FIELDS.size)
    )
    statics = tuple(
        _contact_from(CONTACT_FIELDS.unpack_from(body, offset))
        for offset in range(statics_at, expected, CONTACT_FIELDS.size)
    )
    return Question(tick, tuple(craft), contacts, statics)


def question_bytes(question: Question) -> bytes:
    """Return a question's bytes, its length field first, as the engine writes."""
    body = bytearray(
        HEADER.pack(
            MAGIC, VERSION, KIND_QUESTION, question.tick, len(question.craft), 0
        )
    )
    for view in question.craft:
        body += VIEW_FIELDS.pack(*_view_values(view))
        body += view.notebook
    body += CONTACT_HEAD.pack(len(question.contacts), 0)
    for contact in question.contacts:
        body += CONTACT_FIELDS.pack(*_values_of(CONTACT_LAYOUT, contact))
    body += CONTACT_HEAD.pack(len(question.statics), 0)
    for contact in question.statics:
        body += CONTACT_FIELDS.pack(*_values_of(CONTACT_LAYOUT, contact))
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
    if actions.target is not None:
        written += U8.pack(ActionKind.TARGET) + U16.pack(actions.target)
        count += 1
    if actions.lasers is not None:
        first, second = actions.lasers
        written += U8.pack(ActionKind.LASERS)
        written += LASERS.pack(first.mode, first.burst, second.mode, second.burst)
        count += 1
    if actions.warhead is not None:
        written += U8.pack(ActionKind.WARHEAD)
        written += WARHEAD.pack(actions.warhead.launcher, int(actions.warhead.paired))
        count += 1
    if actions.turrets is not None:
        written += U8.pack(ActionKind.TURRETS) + U8.pack(len(actions.turrets))
        for turret in actions.turrets:
            written += TURRET.pack(turret.slot, turret.target, int(turret.ion))
        count += 1
    if actions.arrival is not None:
        written += U8.pack(ActionKind.ARRIVAL)
        written += ARRIVAL.pack(int(actions.arrival.arriving), actions.arrival.speed)
        count += 1
    if actions.order is not None:
        written += U8.pack(ActionKind.ORDER)
        written += ORDER.pack(actions.order.slot, actions.order.state)
        count += 1
    if actions.depart:
        written += U8.pack(ActionKind.DEPART)
        count += 1
    if actions.dock:
        written += U8.pack(ActionKind.DOCK)
        count += 1
    if actions.countermeasure:
        written += U8.pack(ActionKind.COUNTERMEASURE)
        count += 1
    if actions.evaded:
        written += U8.pack(ActionKind.EVADED)
        count += 1
    if actions.maneuver_timer is not None:
        written += U8.pack(ActionKind.MANEUVER_TIMER)
        written += U32.pack(actions.maneuver_timer)
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


def _take_turrets(reader: _Reader) -> tuple[Turret, ...]:
    (count,) = reader.take(U8)
    turrets = []
    for _ in range(count):
        slot, target, ion = reader.take(TURRET)
        if ion > 1:
            raise ValueError(f"ion is {ion}, not 0 or 1")
        turrets.append(Turret(slot, target, bool(ion)))
    return tuple(turrets)


def _take_action(reader: _Reader, kind: ActionKind) -> t.Any:
    if kind == ActionKind.STEER:
        return Steer(*reader.take(STEER))
    if kind == ActionKind.ACTIVITY:
        return reader.take(U8)[0]
    if kind == ActionKind.LASERS:
        mode0, burst0, mode1, burst1 = reader.take(LASERS)
        return (CannonGroup(mode0, burst0), CannonGroup(mode1, burst1))
    if kind == ActionKind.WARHEAD:
        launcher, paired = reader.take(WARHEAD)
        if paired > 1:
            raise ValueError(f"paired is {paired}, not 0 or 1")
        return Warhead(launcher, bool(paired))
    if kind == ActionKind.TURRETS:
        return _take_turrets(reader)
    if kind == ActionKind.ARRIVAL:
        arriving, speed = reader.take(ARRIVAL)
        if arriving > 1:
            raise ValueError(f"arriving is {arriving}, not 0 or 1")
        return Arrival(bool(arriving), speed)
    if kind == ActionKind.ORDER:
        return OrderMove(*reader.take(ORDER))
    if kind in (
        ActionKind.DEPART,
        ActionKind.DOCK,
        ActionKind.COUNTERMEASURE,
        ActionKind.EVADED,
    ):
        return True
    if kind == ActionKind.MANEUVER_TIMER:
        return reader.take(U32)[0]
    return reader.take(U16)[0]


def _read_actions(reader: _Reader) -> Actions:
    found: dict[str, t.Any] = {}
    (count,) = reader.take(U8)
    for _ in range(count):
        (code,) = reader.take(U8)
        try:
            kind = ActionKind(code)
        except ValueError:
            raise WireError(f"action kind {code} is not one of the fifteen") from None
        name = kind.name.lower()
        if name in found:
            raise WireError(f"action kind {code} is sent twice")
        try:
            found[name] = _take_action(reader, kind)
        except ValueError as err:
            if isinstance(err, WireError):
                raise
            raise WireError(f"an action is out of range: {err}") from err
    try:
        return Actions(**found)
    except ValueError as err:
        raise WireError(f"an action is out of range: {err}") from err


def _check_order(order: OrderMove, view: CraftView) -> None:
    if view.leader != NO_OBJECT:
        raise WireError(
            f"craft {view.object_index} follows a leader; it sends no order"
        )
    allowed = order.slot == view.order_slot
    if order.slot < ORDER_SLOTS - 1:
        allowed |= view.order_codes[order.slot] != 0 and not view.books & Books.SKIPPED
    else:
        allowed |= bool(view.books & Books.SKIP_DUE)
    if not allowed:
        raise WireError(f"order slot {order.slot} is not one the view allows")


def check_shown(actions: Actions, view: CraftView, question: Question) -> None:
    """Raise WireError when the actions name what the question did not show.

    The engine's reader refuses the whole answer for: a target that is
    neither NO_OBJECT, a contact nor a static, a turret target that is
    neither NO_OBJECT nor a contact, a firing cannon group
    the view does not count, a launcher the view does not count or shows
    cooling down or empty, a turret slot the view's turret mask does not name, an
    order from a craft that follows a leader or to a slot other than its
    own, one of 0 to 2 with an order (not after a skip to order 4) or 3 with
    the skip due, a depart or dock the view's books do not allow, a
    countermeasure the view does not show ready, and an evaded with no
    evade command in the view's commands.
    """
    shown = {contact.object_index for contact in question.contacts} | {NO_OBJECT}
    targets = shown | {static.object_index for static in question.statics}
    if actions.target is not None and actions.target not in targets:
        raise WireError(f"target {actions.target} is not a contact or a static")
    for group, cannons in enumerate(actions.lasers or ()):
        if cannons.burst and group >= view.cannon_group_count:
            raise WireError(f"cannon group {group} is not one of the craft's")
    warhead = actions.warhead
    if warhead is not None and (
        warhead.launcher >= view.warhead_launcher_count
        or view.launcher_cooldown[warhead.launcher] != 0
        or view.warheads_left[warhead.launcher] == 0
    ):
        raise WireError(f"launcher {warhead.launcher} is not shown ready")
    for turret in actions.turrets or ():
        if not view.turret_slot_mask >> turret.slot & 1:
            raise WireError(f"weapon slot {turret.slot} is not a turret")
        if turret.target not in shown:
            raise WireError(f"turret target {turret.target} is not a contact")
    if actions.order is not None:
        _check_order(actions.order, view)
    if actions.depart and not view.books & Books.MAY_JUMP:
        raise WireError("depart is not one the view's books allow")
    if actions.dock and not view.books & Books.AT_HANGAR:
        raise WireError("dock is not one the view's books allow")
    if actions.countermeasure and not view.countermeasure_ready:
        raise WireError("the countermeasure is not shown ready")
    if actions.evaded and not view.commands & Commands.EVADE:
        raise WireError("evaded with no evade command shown")


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
        actions = _read_actions(reader)
        check_shown(actions, view, question)
        craft.append(CraftAnswer(object_index, notebook, actions))
    if reader.offset != len(body):
        raise WireError(
            f"the answer has {len(body) - reader.offset} bytes past its end"
        )
    return Answer(tick, tuple(craft))

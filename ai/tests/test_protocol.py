"""The engine's C and the program's Python share every protocol number.

Each constant both sides use is read from the engine's headers and compared
with the program's own, so a change on one side fails here until the other
side follows.
"""

from __future__ import annotations

import dataclasses
import pathlib
import re

from conftest import ENGINE

from jedimaster_ai import wire
from jedimaster_ai import world
from jedimaster_ai.behaviors import arrival
from jedimaster_ai.behaviors import countermeasures
from jedimaster_ai.orders import OrderCode
from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Answer
from jedimaster_ai.world import Arrival
from jedimaster_ai.world import CannonGroup
from jedimaster_ai.world import Contact
from jedimaster_ai.world import CraftAnswer
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import OrderMove
from jedimaster_ai.world import Steer
from jedimaster_ai.world import Turret
from jedimaster_ai.world import Warhead

AI_WIRE_H = ENGINE / "src" / "xvt_runtime" / "ai" / "ai_wire.h"
AI_STATE_H = ENGINE / "src" / "xvt" / "flight" / "ai_state.h"
MISSION_H = ENGINE / "src" / "xvt" / "flight" / "mission" / "mission.h"
MISSION_C = ENGINE / "src" / "xvt" / "flight" / "mission" / "mission.c"
C_TYPES = {
    "uint8_t": "B",
    "uint16_t": "H",
    "int16_t": "h",
    "int32_t": "i",
    "uint32_t": "I",
}
# Array sizes the wire's structs name, by the program's own numbers.
C_SIZES = {
    "AI_WIRE_CANNON_GROUPS": world.CANNON_GROUPS,
    "AI_WIRE_WARHEAD_LAUNCHERS": world.WARHEAD_LAUNCHERS,
    "AI_NOTEBOOK_BYTES": NOTEBOOK_BYTES,
    "AI_WIRE_ORDER_SLOTS": world.ORDER_SLOTS,
    "AI_WIRE_POINT_AXES": world.POINT_AXES,
    "AI_WIRE_THREATS_MAX": world.THREATS_MAX,
    "AI_WIRE_WAYPOINTS": world.WAYPOINTS,
}


def _without_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def _value(text: str, known: dict[str, int]) -> int:
    """Return a C constant's value: literals and names joined by +."""
    total = 0
    for term in text.split("+"):
        term = term.strip()
        total += known[term] if term in known else int(term, 0)
    return total


def c_enum(path: pathlib.Path, name: str, prefix: str) -> dict[str, int]:
    """Return a C enum's members, prefix dropped, with their values."""
    text = _without_comments(path.read_text())
    body = re.search(rf"enum {name} \{{(.*?)\}};", text, re.S).group(1)
    members: dict[str, int] = {}
    known = {"AI_NOTEBOOK_BYTES": NOTEBOOK_BYTES}
    for entry in body.split(","):
        key, _, value = " ".join(entry.split()).partition("=")
        if key:
            members[key.strip().removeprefix(prefix)] = _value(value, known)
    return members


def test_notebook_bytes_match() -> None:
    text = AI_STATE_H.read_text()
    assert int(re.search(r"AI_NOTEBOOK_BYTES = (\d+)", text).group(1)) == NOTEBOOK_BYTES


def test_wire_constants_match() -> None:
    constants = c_enum(AI_WIRE_H, "ai_wire_constant", "AI_WIRE_")
    every_action = Actions(
        Steer(1, 2, 3),
        4,
        Activity.WAITING,
        5,
        target=6,
        lasers=(CannonGroup(1, 1), CannonGroup()),
        warhead=Warhead(1),
        turrets=tuple(Turret(slot, 7) for slot in range(world.WEAPON_SLOTS)),
        arrival=Arrival(True, world.ARRIVAL_SPEED_MAX),
        order=OrderMove(1, 2),
        depart=True,
        countermeasure=True,
        evaded=True,
    )
    answer = Answer(1, (CraftAnswer(1, bytes(NOTEBOOK_BYTES), every_action),))
    assert constants == {
        "MAGIC": wire.MAGIC,
        "VERSION": wire.VERSION,
        "KIND_QUESTION": wire.KIND_QUESTION,
        "KIND_ANSWER": wire.KIND_ANSWER,
        "HEAD_BYTES": wire.HEAD_BYTES,
        "CRAFT_VIEW_BYTES": wire.CRAFT_VIEW_BYTES,
        "CONTACT_HEAD_BYTES": wire.CONTACT_HEAD.size,
        "CONTACT_BYTES": wire.CONTACT_FIELDS.size,
        "CONTACTS_MAX": world.CONTACTS_MAX,
        "CANNON_GROUPS": world.CANNON_GROUPS,
        "WARHEAD_LAUNCHERS": world.WARHEAD_LAUNCHERS,
        "WEAPON_SLOTS": world.WEAPON_SLOTS,
        "CRAFT_ANSWER_BYTES_MAX": len(wire.answer_bytes(answer)) - wire.HEAD_BYTES,
        "ARRIVAL_SPEED_MAX": world.ARRIVAL_SPEED_MAX,
        "ORDER_SLOTS": world.ORDER_SLOTS,
        "POINT_AXES": world.POINT_AXES,
        "THREATS_MAX": world.THREATS_MAX,
        "WAYPOINTS": world.WAYPOINTS,
    }


def test_books_bits_match() -> None:
    books = c_enum(AI_WIRE_H, "ai_wire_books", "AI_WIRE_BOOKS_")
    assert books == {bit.name: bit.value for bit in world.Books}


def test_commands_bits_match() -> None:
    bits = c_enum(AI_WIRE_H, "ai_wire_commands", "AI_WIRE_COMMAND_")
    assert bits == {bit.name: bit.value for bit in world.Commands}


def test_countermeasures_match() -> None:
    kinds = c_enum(AI_WIRE_H, "ai_wire_countermeasure", "AI_WIRE_COUNTERMEASURE_")
    assert kinds == {kind.name: kind.value for kind in world.Countermeasure}


def test_concussion_missile_types_match() -> None:
    text = (ENGINE / "src" / "xvt" / "flight" / "object" / "laser.h").read_text()
    found = {
        name: int(re.search(rf"WARHEAD_OBJECT_TYPE_{name} = (\d+),", text).group(1))
        for name in ("CONCUSSION_MISSILE", "ADVANCED_CONCUSSION_MISSILE")
    }
    assert found == {
        "CONCUSSION_MISSILE": countermeasures.CONCUSSION_MISSILE,
        "ADVANCED_CONCUSSION_MISSILE": countermeasures.ADVANCED_CONCUSSION_MISSILE,
    }


def test_action_kinds_match() -> None:
    kinds = c_enum(AI_WIRE_H, "ai_wire_action_kind", "AI_WIRE_ACTION_")
    assert kinds.pop("KIND_END") == len(wire.ActionKind) + 1
    assert kinds == {kind.name: kind.value for kind in wire.ActionKind}


def c_struct(name: str) -> list[tuple[str, str, int]]:
    """Return a C struct's members as (name, struct code, count)."""
    text = _without_comments(AI_WIRE_H.read_text())
    body = re.search(rf"struct {name} \{{(.*?)\}};", text, re.S).group(1)
    members = re.findall(r"(\w+) (\w+)(?:\[(\w+)\])?;", body)
    return [
        (member, C_TYPES[kind], C_SIZES[size] if size else 1)
        for kind, member, size in members
    ]


def test_craft_view_fields_match() -> None:
    members = c_struct("ai_wire_craft_view")
    assert members[-1] == ("notebook", "B", NOTEBOOK_BYTES)
    assert members[:-1] == list(wire.VIEW_LAYOUT)


def test_contact_fields_match() -> None:
    members = c_struct("ai_wire_contact")
    assert [(name, code) for name, code, _ in members] == list(wire.CONTACT_LAYOUT)
    assert {count for _, _, count in members} == {1}
    assert [name for name, _ in wire.CONTACT_LAYOUT] == [
        field.name for field in dataclasses.fields(Contact)
    ]


def test_contact_flags_match() -> None:
    flags = c_enum(AI_WIRE_H, "ai_wire_contact_flag", "AI_WIRE_CONTACT_")
    assert flags == {"PLAYER": 1}
    assert Contact(*([0] * 7), flags["PLAYER"], *([0] * 14)).player_flown


def test_activities_match() -> None:
    activities = c_enum(AI_STATE_H, "ai_activity", "AI_ACTIVITY_")
    assert activities.pop("COUNT") == len(Activity)
    assert activities == {activity.name: activity.value for activity in Activity}


def test_order_codes_match() -> None:
    orders = c_enum(MISSION_H, "mission_order_code", "MISSION_ORDER_")
    assert orders == {order.name: order.value for order in OrderCode}


def test_stopped_orders_match() -> None:
    text = _without_comments(MISSION_C.read_text())
    body = re.search(
        r"int mission_order_starts_stopped\(int order_code\)\s*\{(.*?)return 1;",
        text,
        re.S,
    ).group(1)
    names = re.findall(r"case MISSION_ORDER_(\w+):", body)
    assert {OrderCode[name] for name in names} == arrival.STOPPED_ORDERS

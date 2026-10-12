"""What the engine tells the AI program about craft, and what it answers.

Purpose:
    The meaning of the messages between the engine and the AI program, apart
    from their bytes (``wire`` turns these into bytes and back). The field
    names are the engine's, from ``engine/src/xvt_runtime/ai/ai_wire.h``.

Invariants:
    - Every number is an integer in the engine's units: angles are 65,536 to
      a full turn, throttle is a fraction of 65,536.
    - A craft answer carries each action kind at most once: ``Actions`` has
      one field per kind, and a field left None sends nothing.
    - A value that the engine would refuse fails here, where it is made,
      with ValueError: an answer the program writes always passes the
      engine's reader.
"""

from __future__ import annotations

import dataclasses
import enum

NOTEBOOK_BYTES = 64
U16_MAX = 0xFFFF


class Activity(enum.IntEnum):
    """What a craft is doing, as the engine reads it (``enum ai_activity``)."""

    NONE = 0
    HOLDING_STATION = 1
    IN_FORMATION = 2
    WAITING_FOR_GO = 3
    WAITING = 4
    ARRIVING_FROM_HYPERSPACE = 5
    LEAVING_BY_HYPERSPACE = 6
    LEAVING_HANGAR = 7
    ENTERING_HANGAR = 8
    GOING_HOME = 9
    GOING_HOME_EVADING = 10
    BOARDING = 11
    DISABLED = 12


@dataclasses.dataclass(frozen=True)
class CraftView:
    """One craft due to think, as a question describes it.

    ``order_code`` is the mission order the craft works on (``orders.
    OrderCode``); the engine may send a code no order has. ``notebook`` is
    the craft's NOTEBOOK_BYTES of memory, as the program last wrote it, or
    all zero bytes before the program first writes it.
    """

    object_index: int
    object_signature: int
    object_type: int
    flight_group: int
    order_code: int
    order_slot: int
    activity: int
    team: int
    skill: int
    think_interval: int
    throttle: int
    x: int
    y: int
    z: int
    yaw: int
    pitch: int
    roll: int
    speed: int
    random_seed: int
    notebook: bytes


@dataclasses.dataclass(frozen=True)
class Question:
    """The craft due to think at one reference step, in the engine's order."""

    tick: int
    craft: tuple[CraftView, ...]


def _check_u16(name: str, value: int, low: int = 0) -> None:
    if not low <= value <= U16_MAX:
        raise ValueError(f"{name} {value!r} is outside {low} to {U16_MAX}")


@dataclasses.dataclass(frozen=True)
class Steer:
    """The heading, pitch and roll a craft turns to."""

    yaw: int
    pitch: int
    roll: int

    def __post_init__(self) -> None:
        _check_u16("yaw", self.yaw)
        _check_u16("pitch", self.pitch)
        _check_u16("roll", self.roll)


@dataclasses.dataclass(frozen=True)
class Actions:
    """What one craft is told to do; None sends nothing of that kind.

    ``throttle`` is a fraction of 65,536. ``think_interval`` is the ticks
    until the craft is asked about again, 1 or more.
    """

    steer: Steer | None = None
    throttle: int | None = None
    activity: Activity | None = None
    think_interval: int | None = None

    def __post_init__(self) -> None:
        if self.throttle is not None:
            _check_u16("throttle", self.throttle)
        if self.activity is not None:
            object.__setattr__(self, "activity", Activity(self.activity))
        if self.think_interval is not None:
            _check_u16("think_interval", self.think_interval, low=1)


NO_ACTIONS = Actions()


@dataclasses.dataclass(frozen=True)
class CraftAnswer:
    """One craft's answer: its new notebook and what it is told to do."""

    object_index: int
    notebook: bytes
    actions: Actions = NO_ACTIONS

    def __post_init__(self) -> None:
        if len(self.notebook) != NOTEBOOK_BYTES:
            raise ValueError(
                f"craft {self.object_index}'s notebook holds "
                f"{len(self.notebook)} bytes, not {NOTEBOOK_BYTES}"
            )


@dataclasses.dataclass(frozen=True)
class Answer:
    """The answer to one question: one craft answer per craft, in its order."""

    tick: int
    craft: tuple[CraftAnswer, ...]

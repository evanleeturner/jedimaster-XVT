"""Shared test values: a craft view and a contact with every field set, changed
per test."""

from __future__ import annotations

import dataclasses
import logging
import pathlib
import typing as t

import pytest

from jedimaster_ai.world import Books
from jedimaster_ai.world import Contact
from jedimaster_ai.world import CraftView
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import NOTEBOOK_BYTES

ENGINE = pathlib.Path(__file__).resolve().parents[2] / "engine"

VIEW = CraftView(
    object_index=4,
    object_signature=7,
    object_type=3,
    flight_group=2,
    order_code=7,
    order_slot=0,
    activity=0,
    team=1,
    skill=2,
    think_interval=59,
    throttle=100,
    x=-5,
    y=6,
    z=7,
    yaw=100,
    pitch=200,
    roll=300,
    speed=40,
    random_seed=-3,
    order_target_mask=0b10,
    turret_slot_mask=0b1000,
    cannon_group_count=1,
    warhead_launcher_count=1,
    cannon_type=(56, 0),
    warhead_type=(60, 0),
    warheads_left=(4, 0),
    launcher_cooldown=(0, 0),
    enemy_mask=0b10,
    leader=0xFFFF,
    order_codes=(7, 0, 0, 0),
    order_states=(0, 0, 0, 0),
    order_throttle=0,
    order_variable1=0,
    order_variable2=0,
    order_speed=0,
    full_speed=300,
    books=Books.TARGETS_LEFT,
    mothership=0xFFFF,
    outside_hangar=(0, 0, 0),
    inside_hangar=(0, 0, 0),
    home_point=(0, 0, 0),
    countermeasure=0,
    countermeasures_left=0,
    countermeasure_working=0,
    threat_count=0,
    chaff_seconds=0,
    countermeasure_cooldown=0,
    threat_object=(0, 0, 0, 0),
    threat_type=(0, 0, 0, 0),
    threat_decoyed=(0, 0, 0, 0),
    threat_x=(0, 0, 0, 0),
    threat_y=(0, 0, 0, 0),
    threat_z=(0, 0, 0, 0),
    waypoint_mask=0,
    waypoint_x=(0,) * 8,
    waypoint_y=(0,) * 8,
    waypoint_z=(0,) * 8,
    commands=0,
    command_target=NO_OBJECT,
    avoid_target=NO_OBJECT,
    place=(0, 0, 0),
    lock_ticks=0,
    lock_needed=0,
    lock_state=0,
    skill_level=0,
    notebook=bytes(NOTEBOOK_BYTES),
)
# Contact 0 is VIEW's own craft; contact 1 another craft, its order's target.
CONTACT = Contact(
    object_index=9,
    object_signature=11,
    object_type=5,
    genus=1,
    team=2,
    flight_group=3,
    object_kind=0,
    flags=0,
    x=1000,
    y=-2000,
    z=300,
    yaw=0x4000,
    pitch=0,
    speed=30,
    target=4,
    last_attacker=0xFFFF,
    front_shield=400,
    rear_shield=-1,
    hull_damage=10,
    hull_strength=200,
    activity=0,
    size=0,
    warheads_coming=0,
    warheads_coming_damage=0,
)


@pytest.fixture
def view() -> t.Callable[..., CraftView]:
    """Return a maker of VIEW with the named fields changed."""

    def make(**changes: t.Any) -> CraftView:
        return dataclasses.replace(VIEW, **changes)

    return make


@pytest.fixture
def contact() -> t.Callable[..., Contact]:
    """Return a maker of CONTACT with the named fields changed."""

    def make(**changes: t.Any) -> Contact:
        return dataclasses.replace(CONTACT, **changes)

    return make


@pytest.fixture(autouse=True)
def package_logger() -> t.Iterator[None]:
    """Put the package's logger back as it was after each test."""
    logger = logging.getLogger("jedimaster_ai")
    kept = (logger.level, logger.propagate, list(logger.handlers))
    yield
    for handler in logger.handlers:
        if handler not in kept[2]:
            handler.close()
    logger.setLevel(kept[0])
    logger.propagate = kept[1]
    logger.handlers[:] = kept[2]

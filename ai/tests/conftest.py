"""Shared test values: a craft view with every field set, changed per test."""

from __future__ import annotations

import dataclasses
import logging
import pathlib
import typing as t

import pytest

from jedimaster_ai.world import CraftView
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
    notebook=bytes(NOTEBOOK_BYTES),
)


@pytest.fixture
def view() -> t.Callable[..., CraftView]:
    """Return a maker of VIEW with the named fields changed."""

    def make(**changes: t.Any) -> CraftView:
        return dataclasses.replace(VIEW, **changes)

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

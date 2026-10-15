"""The launcher's option defaults equal the engine's own starting values.

Purpose:
    Fence the ruling that the launcher holds a real value for each of the
    seven game options: those values are the ones ``engine/resources/config.yaml``
    (``game:``) starts with. If the engine's file or the launcher's table
    moves, this fails and names the option.

Flow:
    Read the ``game:`` block of the engine's config by lines (no YAML
    dependency), turn each number through the engine's own encodings, and
    compare with ``OPTIONS[name].default``.

Invariants:
    - The encodings are the engine's: ``difficulty`` 0..3 is easy, medium,
      hard, easy_cheat (``engine/src/xvt/frontend/config.h`` 19-22);
      switches are 1 on and 0 off; ``handicapping`` 0..2 is 2..4 wins
      (``engine/src/xvt_runtime/runtime/launch_description.c`` 262-266,
      ``battle_length - 2``); ``mission_time_limit`` 255 is default, 0 is
      none and n is n minutes (same file 266-282); ``continue_sequence``
      1 is continue and 0 is restart
      (``engine/src/xvt/frontend/mission_setup.h`` 57-58).

Call:
    ``pytest tests/test_launch_option_defaults.py``
"""

from __future__ import annotations

import logging
from pathlib import Path

import pytest

from jedimaster.launch.options import OPTIONS

logger = logging.getLogger(__name__)

CONFIG = Path(__file__).resolve().parents[2] / "engine" / "resources" / "config.yaml"
GAME_KEYS = (
    "difficulty",
    "collisions",
    "craft_jumping",
    "random_setup",
    "handicapping",
    "mission_time_limit",
    "continue_sequence",
)


def _game_block() -> dict[str, int]:
    """Return the integer keys that sit directly under ``game:`` (two-space indent)."""
    found: dict[str, int] = {}
    inside = False
    for line in CONFIG.read_text(encoding="utf-8").splitlines():
        if not line.startswith(" ") and line.strip():
            inside = line.rstrip() == "game:"
            continue
        if inside and line.startswith("  ") and not line.startswith("   "):
            key, _, value = line.strip().partition(":")
            if key in GAME_KEYS:
                found[key] = int(value.strip())
    return found


def _minutes(number: int) -> str:
    return {255: "default", 0: "none"}.get(number, str(number))


ENCODE = {
    "difficulty": lambda n: ("easy", "medium", "hard", "easy_cheat")[n],
    "collisions": lambda n: ("off", "on")[n],
    "craft_jumping": lambda n: ("off", "on")[n],
    "random_setup": lambda n: ("off", "on")[n],
    "battle_length": lambda n: str(n + 2),
    "mission_time_limit": _minutes,
    "continue_sequence": lambda n: ("restart", "continue")[n],
}


NO_ENGINE_KEY = {
    "sith": "the engine reads the Sith setting from the description and the "
    "network, never from config.yaml; a description without it means none"
}
"""Options with no ``game:`` key in the engine's config, each with its reason."""


def test_every_option_is_checked_against_the_engine_or_exempt_by_name():
    assert set(OPTIONS) == set(ENCODE) | set(NO_ENGINE_KEY)
    assert OPTIONS["sith"].default == "none"


def test_the_engine_config_holds_all_seven_keys():
    assert set(_game_block()) == set(GAME_KEYS)


@pytest.mark.parametrize("name", sorted(ENCODE))
def test_the_launcher_default_is_the_engines_starting_value(name):
    block = _game_block()
    key = "handicapping" if name == "battle_length" else name
    expected = ENCODE[name](block[key])
    logger.debug("option %s: engine %s -> %s", name, block[key], expected)
    assert OPTIONS[name].default == expected

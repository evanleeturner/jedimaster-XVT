"""Fence for world: an answer the engine would refuse cannot be made."""

from __future__ import annotations

import pytest

from jedimaster_ai.world import Actions
from jedimaster_ai.world import Activity
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import CraftAnswer
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import Steer
from jedimaster_ai.world import Turret


@pytest.mark.parametrize(
    "changes",
    [
        {"throttle": -1},
        {"throttle": 0x10000},
        {"think_interval": 0},
        {"think_interval": 0x10000},
        {"activity": len(Activity)},
        {"activity": Activity.ARRIVING_FROM_HYPERSPACE},
        {"activity": Activity.LEAVING_HANGAR},
        {"maneuver_timer": -1},
        {"maneuver_timer": 0x80000000},
    ],
    ids=[
        "throttle low",
        "throttle high",
        "interval 0",
        "interval high",
        "activity",
        "arriving",
        "leaving",
        "timer low",
        "timer high",
    ],  # fmt: skip
)
def test_actions_out_of_range_are_refused(changes: dict[str, int]) -> None:
    with pytest.raises(ValueError):
        Actions(**changes)


@pytest.mark.parametrize("angles", [(-1, 0, 0), (0, 0x10000, 0), (0, 0, -1)])
def test_steer_out_of_range_is_refused(angles: tuple[int, int, int]) -> None:
    with pytest.raises(ValueError):
        Steer(*angles)


def test_actions_at_their_limits_are_made() -> None:
    actions = Actions(Steer(0, 0xFFFF, 0), 0xFFFF, 12, 1)
    assert actions.activity is Activity.DISABLED
    assert Actions(maneuver_timer=0).maneuver_timer == 0
    # The engine's maneuver timer is a C int.
    assert Actions(maneuver_timer=0x7FFFFFFF).maneuver_timer == 0x7FFFFFFF


@pytest.mark.parametrize(
    ("flags", "disabled", "attacked"),
    [
        (0, False, False),
        (ContactFlags.PLAYER, False, False),
        (ContactFlags.DISABLED, True, False),
        (ContactFlags.ATTACKED, False, True),
        (ContactFlags.DISABLED | ContactFlags.ATTACKED, True, True),
    ],
)
def test_a_contacts_flags_say_disabled_and_attacked(
    contact, flags, disabled, attacked
) -> None:
    found = contact(flags=flags)
    assert (found.disabled, found.attacked) == (disabled, attacked)


def test_craft_answer_needs_a_whole_notebook() -> None:
    with pytest.raises(ValueError, match="63 bytes"):
        CraftAnswer(4, bytes(NOTEBOOK_BYTES - 1))


def test_a_turret_fires_its_usual_shots_unless_set_to_ion() -> None:
    assert Turret(3, 9).ion is False
    assert Turret(3, 9, True).ion is True

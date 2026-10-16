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
        {"self_destruct": 0},
        {"self_destruct": 56},
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
        "self destruct 0",
        "self destruct 56",
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
    # 1 and 55 units of 1,180 ticks; 56 units would wrap the engine's u16 timer.
    assert Actions(self_destruct=1).self_destruct == 1
    assert Actions(self_destruct=55).self_destruct == 55
    assert Actions().self_destruct is None


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


def test_new_view_and_question_names_default_to_nothing() -> None:
    """Until the engine sends them: no craft escorted, no station, no start
    point, no order with a target now, no static, no static target."""
    from conftest import VIEW

    from jedimaster_ai.world import Books
    from jedimaster_ai.world import NO_OBJECT
    from jedimaster_ai.world import Question

    fields = {
        field: getattr(VIEW, field)
        for field in ("escorted", "station", "start_point", "targets_now")
    }
    assert fields == {
        "escorted": NO_OBJECT,
        "station": (0, 0, 0),
        "start_point": (0, 0, 0),
        "targets_now": 0,
    }
    assert VIEW.static_target_mask == 0
    assert Question(1, (VIEW,)).statics == ()
    assert Books.STATION == 0x1000 and Books.START_POINT == 0x2000


def test_the_static_flag_is_a_contact_flag() -> None:
    assert ContactFlags.STATIC == 8


def test_a_contact_without_a_box_reads_no_box_and_no_roll() -> None:
    """The defaults lane code reads as "no box, use the centre": all zeros."""
    from conftest import CONTACT

    assert CONTACT.box_min == (0, 0, 0)
    assert CONTACT.box_max == (0, 0, 0)
    assert CONTACT.roll == 0


def test_boarding_names_default_to_nothing() -> None:
    """Until the engine sends them: no board point, no boardings done, none
    boarded, no boarder, and the three looking activities."""
    from conftest import CONTACT
    from conftest import VIEW

    from jedimaster_ai.world import Books
    from jedimaster_ai.world import NO_OBJECT

    assert VIEW.board_point == (0, 0, 0)
    assert VIEW.order_progress == (0, 0, 0, 0)
    assert VIEW.boarded == ()
    assert CONTACT.boarder == NO_OBJECT
    assert Books.BOARD_POINT == 0x4000
    assert Books.AT_BOARD_POINT == 0x8000
    assert Books.ALL_LAUNCHED == 0x10000
    assert ContactFlags.PARKED == 0x10 and ContactFlags.AWAITING == 0x20
    assert [Activity.LOOKING_TO_BOARD, Activity.LOOKING_TO_CAPTURE] == [13, 14]
    assert Activity.LOOKING_TO_PICK_UP == 15

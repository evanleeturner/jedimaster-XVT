"""Fence for captured: a captured craft flies home for its captor, or holds.

1997 (``old:paiman.c:3058-3071``) gave a captured craft its fly-home plan
when its model could move, else its stationary plan; a craft picked up is
carried with its systems off (``:3178-3185``). Each way has a test, and the
still answer is fenced field by field.
"""

from __future__ import annotations

from jedimaster_ai import wire
from jedimaster_ai.behaviors import captured
from jedimaster_ai.behaviors import home
from jedimaster_ai.world import Activity
from jedimaster_ai.world import Books
from jedimaster_ai.world import ContactFlags
from jedimaster_ai.world import HOLD_FIRE
from jedimaster_ai.world import NO_OBJECT
from jedimaster_ai.world import NOTEBOOK_BYTES
from jedimaster_ai.world import Question

ME = 4
GOING_OUT = Books.CAPTURED | Books.MAY_JUMP | Books.HOME_POINT
FAR = (0, 0x20000, 0)
# A notebook another behavior wrote, which holding still must not touch.
WRITTEN = bytes(range(1, NOTEBOOK_BYTES + 1))


def ask(contact, me, **mine):
    """Return the captured craft's decision, its own contact first, its
    actions checked against the engine's reader."""
    own = contact(object_index=ME, x=me.x, y=me.y, z=me.z, **mine)
    question = Question(1000, (me,), (own,), ())
    decision = captured.think(me, question)
    wire.check_shown(decision.actions, me, question)
    return decision, question


def test_a_captured_craft_that_can_fly_goes_home(view, contact) -> None:
    me = view(books=GOING_OUT, home_point=FAR)
    decision, question = ask(contact, me)
    assert decision == home.think(me, question)
    assert decision.actions.throttle != 0


def test_a_captured_craft_that_cannot_move_holds_still(view, contact) -> None:
    me = view(
        books=GOING_OUT, home_point=FAR, full_speed=0, activity=7, notebook=WRITTEN
    )
    decision, _ = ask(contact, me)
    actions = decision.actions
    assert actions.throttle == 0
    assert actions.steer is None
    assert actions.target == NO_OBJECT
    assert actions.activity == Activity.NONE
    assert actions.lasers == (HOLD_FIRE, HOLD_FIRE)
    assert actions.depart is False
    assert decision.notebook == me.notebook


def test_a_captured_craft_with_its_systems_off_holds_still(view, contact) -> None:
    me = view(books=GOING_OUT, home_point=FAR)
    decision, _ = ask(contact, me, flags=ContactFlags.DISABLED)
    assert decision.actions.throttle == 0
    assert decision.actions.steer is None


def test_holding_still_sends_no_status_line_it_shows(view, contact) -> None:
    me = view(books=Books.CAPTURED, full_speed=0, activity=Activity.NONE)
    decision, _ = ask(contact, me)
    assert decision.actions.activity is None


def test_holding_still_is_logged(view, contact, caplog) -> None:
    caplog.set_level("DEBUG")
    ask(contact, view(books=Books.CAPTURED), flags=ContactFlags.DISABLED)
    assert f"captured.still object={ME} full_speed=300 disabled=1" in caplog.text

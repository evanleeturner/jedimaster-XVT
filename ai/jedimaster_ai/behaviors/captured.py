"""Captured: a craft another side has captured flies home for its captor,
or holds still.

Purpose:
    A craft boarded on a Capture or Pick Up order changes side: the engine's
    dock gives its object the captor's team and IFF (its flight group keeps
    its own, ruling 128) and marks it CAPTURED in its books. 1997 then gave
    it a plan of its own (``old:paiman.c:3058-3071``): fly home evading when
    its model could move, else hold still where it was. Here, likewise:

    - home (``behaviors.home``) when its full speed is not 0 and its systems
      work: by the way out its books name for a captured craft (its group's
      capture mothership, else hyperspace), evading or fighting back on the
      way as every craft going home does (rulings 33 and 34)
    - else stopped for good: throttle 0, no target, cannons held, no status
      line. A craft picked up is carried with its systems off
      (``old:paiman.c:3178-3185``) and holds so.

    Its old orders no longer count: ``think`` sends it here before
    ``progress``, so its order slot never moves, and it flies on no leader of
    another team (``formation.leader_for``), as 1997 kept a craft flying home
    evading out of its old leader's jump (``old:paiorder.c:1701-1717``). A
    craft its own side takes back loses the mark and flies its order again,
    from the slot it was on.

Invariants:
    - Only the view's books and full speed and the craft's own contact
      decide; a stopped craft's notebook goes back as it came.
    - Its turrets defend, as every craft's do (``turrets.defend``).
"""

from __future__ import annotations

import logging

from .. import tactics
from ..world import Actions
from ..world import Activity
from ..world import CraftView
from ..world import HOLD_FIRE
from ..world import NO_OBJECT
from ..world import Question
from . import Decision
from . import home

logger = logging.getLogger(__name__)


def think(view: CraftView, question: Question) -> Decision:
    """Return the captured craft's move: home when it can fly, else stopped
    for good."""
    own = tactics.own_contact(view, question)
    disabled = own is not None and own.disabled
    if view.full_speed and not disabled:
        return home.think(view, question)
    logger.debug(
        "captured.still object=%d full_speed=%d disabled=%d",
        view.object_index,
        view.full_speed,
        int(disabled),
    )
    actions = Actions(
        throttle=0,
        activity=None if view.activity == Activity.NONE else Activity.NONE,
        target=NO_OBJECT,
        lasers=(HOLD_FIRE, HOLD_FIRE),
    )
    return Decision(view.notebook, actions)

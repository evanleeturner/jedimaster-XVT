"""The starship orders: SS Protect, SS Await Return, SS Patrol and Disable.

Purpose:
    The behaviors that fly SS Protect and SS Protect 2 (orders 24 and 25),
    SS Await Return (22) and SS Patrol and Disable (27).

    - SS Await Return stops where it is, as Hold Steady does (``hold``), until
      every group that goes home into its group is back, the goal
      ``progress.GOALS`` reads from the engine's books. Its 1997 plan
      (starshipwaitreturnpln) flew the stop maneuver: throttle 0, and the
      roll, pitch and turn halted at once. Here a turn already begun runs on
      to the angle it was given, as the player's wait command's stop does;
      the turrets defend, as every craft's do.
    - SS Protect and SS Protect 2, one 1997 plan (starshipprotectpln), fly
      the patrol cruise round the group's waypoints (``patrol``) while the
      turrets guard (``turrets.guard``): the craft's own threats first, then
      the attackers of the craft its order protects. The order is done once
      no target is left (``progress.GOALS``).
    - SS Patrol and Disable is not written yet: drift's, so the craft keeps
      its speed and heading, as before the order had a file of its own.
"""

from __future__ import annotations

from ..world import CraftView
from ..world import Question
from . import Decision
from . import drift
from . import hold
from . import patrol
from . import turrets

await_return = hold.think
patrol_and_disable = drift.think


def protect(view: CraftView, question: Question) -> Decision:
    """Return SS Protect's step: the patrol cruise, the turrets on guard."""
    return turrets.guard(view, question, patrol.think(view, question))

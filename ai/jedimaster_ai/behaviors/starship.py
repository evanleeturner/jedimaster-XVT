"""The starship orders: SS Protect, SS Await Return, SS Launch, SS Patrol and
Disable.

Purpose:
    The behaviors that fly SS Protect and SS Protect 2 (orders 24 and 25),
    SS Await Return (22), SS Launch (23) and SS Patrol and Disable (27).

    - SS Await Return stops where it is, as Hold Steady does (``hold``), until
      every group that goes home into its group is back, the goal
      ``progress.GOALS`` reads from the engine's books. Its 1997 plan
      (starshipwaitreturnpln) flew the stop maneuver: throttle 0, and the
      roll, pitch and turn halted at once. Here a turn already begun runs on
      to the angle it was given, as the player's wait command's stop does;
      the turrets defend, as every craft's do.
    - SS Launch stops in the same way until every group that comes out of
      its group has arrived with no waves left, its own goal in
      ``progress.GOALS``. Its 1997 plan (starshipwaitcreatepln) was SS Await
      Return's but for that goal. The craft launches nothing itself: the
      engine brings those groups out of its hangar by their own arrival
      rules.
    - SS Protect and SS Protect 2, one 1997 plan (starshipprotectpln), fly
      the patrol cruise round the group's waypoints (``patrol``) while the
      turrets guard (``turrets.guard``): the craft's own threats first, then
      the attackers of the craft its order protects. The order is done once
      no target is left (``progress.GOALS``).
    - SS Patrol and Disable, its 1997 plan starshipdisablepln, flies the
      patrol cruise (``patrol``) while the turrets disable (``turrets.
      disable``): the craft's own attackers first, with normal shots, then
      the craft its order names that still work, with ion shots. The order
      is done once no target is left, disabled ones counted, as in 1997
      (``progress.GOALS``).
"""

from __future__ import annotations

from ..world import CraftView
from ..world import Question
from . import Decision
from . import hold
from . import patrol
from . import turrets

await_return = hold.think
await_launch = hold.think


def protect(view: CraftView, question: Question) -> Decision:
    """Return SS Protect's step: the patrol cruise, the turrets on guard."""
    return turrets.guard(view, question, patrol.think(view, question))


def patrol_and_disable(view: CraftView, question: Question) -> Decision:
    """Return SS Patrol and Disable's step: the patrol cruise, the turrets
    disabling."""
    return turrets.disable(view, question, patrol.think(view, question))

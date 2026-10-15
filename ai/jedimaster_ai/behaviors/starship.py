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
    - SS Protect, SS Protect 2 and SS Patrol and Disable are not written yet:
      each is drift's, so the craft keeps its speed and heading, as before the
      orders had a file of their own.
"""

from __future__ import annotations

from . import drift
from . import hold

protect = drift.think
await_return = hold.think
patrol_and_disable = drift.think

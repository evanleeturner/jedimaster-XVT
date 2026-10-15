"""The starship orders: SS Protect, SS Await Return, SS Patrol and Disable.

Purpose:
    The behaviors that fly SS Protect and SS Protect 2 (orders 24 and 25),
    SS Await Return (22) and SS Patrol and Disable (27). Not written yet:
    each is drift's, so the craft keeps its speed and heading, as before
    the orders had a file of their own.
"""

from __future__ import annotations

from . import drift

protect = drift.think
await_return = drift.think
patrol_and_disable = drift.think

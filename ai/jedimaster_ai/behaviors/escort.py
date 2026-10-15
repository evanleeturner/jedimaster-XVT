"""Escort (order 10): keep station beside the order's target, and fight
the craft that come after it.

Purpose:
    The behavior that flies this order. Not written yet: ``think`` is
    drift's, so the craft keeps its speed and heading, as before the order
    had a file of its own.
"""

from __future__ import annotations

from . import drift

think = drift.think

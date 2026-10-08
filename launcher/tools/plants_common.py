"""The plant record and the file names more than one plant table uses.

Purpose:
    Hold the ``Plant`` dataclass that every plant table builds and the
    runner applies, and the paths shared across tables (the install module
    and the command line), so the tables and the runner import one
    definition without importing each other.

Flow:
    ``plants_mission``, ``plants_setup``, ``plants_lists_read`` and
    ``plants_lists_output`` import ``Plant`` (and ``I``, ``CLI``) from here;
    ``plant_faults`` imports the tables and ``Plant``.

Invariants:
    - No plant lives here; nothing here reads or writes a file.

Call:
    ``from plants_common import Plant``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class Plant:
    """One fault: replace ``old`` by ``new`` in ``path``; ``expect`` must fail."""

    id: str
    path: str
    old: str
    new: str
    expect: tuple[str, ...]
    regen_schema: bool = False


I = "jedimaster/install.py"  # noqa: E741
CLI = "jedimaster/__main__.py"

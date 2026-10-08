"""The tables the game reads from strings.txt: their order, kinds and lengths.

Purpose:
    Hold, as data, the order in which the game reads the tables of
    ``strings.txt``, each table's kind and length, and the repeating
    pattern of 47 counts that gives the goal condition rows their lines.
    None of it is in any file of the game: these are the lengths the game's
    program reads, as printed by the engine's text_dump tool, copied here
    from that printout, like the icon tables. The table names are the
    printout's, not the game's. With the icon tables of
    ``jedimaster.icons.tables``, the text colors of
    ``jedimaster.fonts.colors`` and the pilot layout of
    ``jedimaster.pilot.layout``, they are the only game data in this
    package's code.

Flow:
    ``TABLES`` are read in order, every time; ``GENDER_TABLES`` follow, each
    only when some model name has that table's gender. ``goal_lines``
    gives a goal table's row lengths from ``GOAL_PATTERN``; nothing else
    here computes. When the game stops reading the file it shows a line of
    the file's own ``MESSAGES`` table: ``OUT_OF_SYNC_LINE`` or
    ``BAD_MODEL_LINE``.

Invariants:
    - A ``lines``, ``escaped`` or ``models`` table's ``length`` is its line
      count; a ``goal`` table's is its row count, and row ``r`` holds
      ``GOAL_PATTERN[r % 47]`` lines (188 rows, 916 lines, the count the
      printout gives the masculine table).
    - The feminine and neutered tables are laid out like the masculine one.

Call:
    ``for table in TABLES: table.name, table.kind, table.length``
"""

from __future__ import annotations

import logging
from typing import NamedTuple

logger = logging.getLogger(__name__)

LINES = "lines"
"""A table of one entry per line."""
GOAL = "goal"
"""A goal condition table: rows of variants, row lengths from the pattern."""
MODELS = "models"
"""The model names: a gender byte, a byte passed over, the name."""
ESCAPED = "escaped"
"""The in-flight messages: one entry per line, backslash escapes decoded."""


class StringTable(NamedTuple):
    """One table of strings.txt: its printed name, its kind, its length."""

    name: str
    kind: str
    length: int


TABLES: tuple[StringTable, ...] = (
    StringTable("damage_system_names", LINES, 11),
    StringTable("file_error_messages", LINES, 4),
    StringTable("disk_io_messages", LINES, 32),
    StringTable("proving_grounds_status_labels", LINES, 5),
    StringTable("goal_escape", LINES, 1),
    StringTable("goal_conditions_masculine", GOAL, 188),
    StringTable("goal_percentages", LINES, 14),
    StringTable("goal_operators", LINES, 2),
    StringTable("goal_titles", LINES, 9),
    StringTable("goal_conjunctions", LINES, 8),
    StringTable("goal_sides", LINES, 3),
    StringTable("goal_family_names", LINES, 7),
    StringTable("goal_genus_names", LINES, 16),
    StringTable("map_room_text", LINES, 20),
    StringTable("in_flight_messages", ESCAPED, 417),
    StringTable("cmd_threat_display_text", LINES, 18),
    StringTable("waypoint_names", LINES, 14),
    StringTable("mesh_component_names", LINES, 33),
    StringTable("cockpit_overlay_text", LINES, 40),
    StringTable("threat_display_text", LINES, 4),
    StringTable("status_strings", LINES, 9),
    StringTable("warhead_names", LINES, 13),
    StringTable("unknown", LINES, 1),
    StringTable("sat_mine_probe_buoy_pilot_names", LINES, 16),
    StringTable("model_names", MODELS, 73),
    StringTable("species_names_plural", LINES, 73),
    StringTable("wingman_commands", LINES, 10),
)
"""The tables read every time, in the game's order."""

MESSAGES = "file_error_messages"
"""The table whose lines the game shows when it stops reading the file."""
OUT_OF_SYNC_LINE = 2
"""The ``MESSAGES`` line (from 0) shown when the file ends before a table does."""
BAD_MODEL_LINE = 3
"""The ``MESSAGES`` line (from 0) shown for a model name's bad first byte."""

GENDER_TABLES: tuple[tuple[int, StringTable], ...] = (
    (1, StringTable("goal_conditions_feminine", GOAL, 188)),
    (2, StringTable("goal_conditions_neutered", GOAL, 188)),
)
"""The tables read last, each only when some model name has its gender."""

# fmt: off
GOAL_PATTERN: tuple[int, ...] = (
    1, 14, 14, 14, 14, 14, 14, 14, 14, 1,
    1, 1, 14, 1, 1, 1, 1, 1, 1, 14,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 14, 14, 14, 14,
)
# fmt: on
"""The lines of goal condition row ``r``: ``GOAL_PATTERN[r % 47]``."""


def goal_lines(rows: int) -> list[int]:
    """Return the line count of each of a goal table's ``rows`` rows.

    Row ``r`` holds ``GOAL_PATTERN[r % len(GOAL_PATTERN)]`` lines. Returns
    ``[]`` for 0 rows. Does not check ``rows`` against any table.
    """
    return [GOAL_PATTERN[r % len(GOAL_PATTERN)] for r in range(rows)]

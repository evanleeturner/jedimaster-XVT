"""Fence for drift: a craft keeps its notebook and is told nothing.

The engine's own test of the program, ``engine/tests/test_ai_program.c``,
checks the same at the other end of the pipe.
"""

from __future__ import annotations

import pytest

from jedimaster_ai.behaviors import Decision
from jedimaster_ai.behaviors import drift
from jedimaster_ai.world import NOTEBOOK_BYTES


@pytest.mark.parametrize(
    "notebook",
    [bytes(NOTEBOOK_BYTES), bytes(range(NOTEBOOK_BYTES))],
    ids=["blank", "written"],
)
def test_drift_keeps_the_notebook_and_sends_nothing(view, notebook: bytes) -> None:
    assert drift.think(view(notebook=notebook), 1000) == Decision(notebook)

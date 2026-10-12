"""A craft's notebook: the memory the AI program keeps in the craft's record.

Purpose:
    The program keeps nothing between questions. What a behavior must
    remember about a craft, it writes in the craft's notebook, and the
    engine sends it back with the next question. The engine saves it with
    the craft, so it survives a saved game and a rewind.

The layout, little-endian, NOTEBOOK_BYTES in all:

    offset  bytes  field
    0       1      layout: LAYOUT, or 0 for a notebook never written
    1       1      behavior: the code of the behavior that wrote it
    2       1      phase: that behavior's step
    3       1      zero
    4       4      dice: the craft's random state (``dice``)
    8       4      phase_tick: the tick its phase began
    12      52     scratch: the behavior's own bytes

Invariants:
    - The engine starts every notebook as zero bytes, a blank notebook.
    - A notebook another behavior wrote is started again for this one; the
      craft's random state carries over, so its numbers never repeat.
    - A notebook that cannot be read (an unknown layout, a byte that must be
      0, a random state of 0) is started again too, after one WARN line.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from . import dice
from .world import CraftView
from .world import NOTEBOOK_BYTES

logger = logging.getLogger(__name__)

LAYOUT = 1
FIXED = struct.Struct("<BBBBII")
SCRATCH_BYTES = NOTEBOOK_BYTES - FIXED.size
U32_MAX = 0xFFFFFFFF


class NotebookError(ValueError):
    """Notebook bytes that are not a notebook of this layout."""


@dataclasses.dataclass(frozen=True)
class Notebook:
    """A written notebook's fields; ``encode`` gives its bytes."""

    behavior: int
    phase: int
    dice: int
    phase_tick: int
    scratch: bytes = bytes(SCRATCH_BYTES)

    def __post_init__(self) -> None:
        if not 1 <= self.behavior <= 0xFF:
            raise ValueError(f"behavior {self.behavior!r} is outside 1 to 255")
        if not 0 <= self.phase <= 0xFF:
            raise ValueError(f"phase {self.phase!r} is outside 0 to 255")
        if not 1 <= self.dice <= U32_MAX:
            raise ValueError(f"dice {self.dice!r} is outside 1 to {U32_MAX}")
        if not 0 <= self.phase_tick <= U32_MAX:
            raise ValueError(
                f"phase_tick {self.phase_tick!r} is outside 0 to {U32_MAX}"
            )
        if len(self.scratch) != SCRATCH_BYTES:
            raise ValueError(
                f"scratch holds {len(self.scratch)} bytes, not {SCRATCH_BYTES}"
            )

    def encode(self) -> bytes:
        """Return the notebook's NOTEBOOK_BYTES."""
        fixed = FIXED.pack(
            LAYOUT, self.behavior, self.phase, 0, self.dice, self.phase_tick
        )
        return fixed + self.scratch

    def in_phase(self, phase: int, tick: int) -> Notebook:
        """Return this notebook moved to a phase that begins at tick."""
        return dataclasses.replace(self, phase=phase, phase_tick=tick)


def decode(raw: bytes) -> Notebook | None:
    """Return the notebook these bytes hold, or None for a blank one."""
    if len(raw) != NOTEBOOK_BYTES:
        raise NotebookError(f"a notebook of {len(raw)} bytes, not {NOTEBOOK_BYTES}")
    layout, behavior, phase, zero, state, phase_tick = FIXED.unpack_from(raw)
    if layout == 0:
        return None
    if layout != LAYOUT or zero != 0:
        raise NotebookError(
            f"layout {layout} with byte 3 = {zero} is not layout {LAYOUT}"
        )
    try:
        return Notebook(behavior, phase, state, phase_tick, raw[FIXED.size :])
    except ValueError as err:
        raise NotebookError(str(err)) from err


def open_for(view: CraftView, behavior: int, tick: int) -> Notebook:
    """Return the craft's notebook for a behavior to read and change.

    The notebook as written when this behavior wrote it; otherwise a new
    one in phase 0 from this tick, keeping the craft's random state when
    the old notebook has one.
    """
    try:
        kept = decode(view.notebook)
    except NotebookError as err:
        logger.warning(
            'notebook.unreadable object=%d behavior=%d error="%s"',
            view.object_index,
            behavior,
            err,
        )
        kept = None
    if kept is not None and kept.behavior == behavior:
        return kept
    state = (
        kept.dice
        if kept is not None
        else dice.seed(view.random_seed, view.object_index)
    )
    return Notebook(behavior, 0, state, tick)

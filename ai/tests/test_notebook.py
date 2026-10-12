"""Fence for notebook: its byte layout and how a behavior opens one."""

from __future__ import annotations

import pytest

from jedimaster_ai import dice
from jedimaster_ai import notebook
from jedimaster_ai.notebook import Notebook
from jedimaster_ai.world import NOTEBOOK_BYTES

SCRATCH = b"\xee" * notebook.SCRATCH_BYTES


def test_encode_writes_the_layout() -> None:
    written = Notebook(3, 2, 0x01020304, 0x0A0B0C0D, SCRATCH).encode()
    assert written == bytes.fromhex("01030200040302010d0c0b0a") + SCRATCH
    assert len(written) == NOTEBOOK_BYTES


def test_decode_reads_what_encode_wrote() -> None:
    kept = Notebook(200, 9, 77, 1234, SCRATCH)
    assert notebook.decode(kept.encode()) == kept


def test_a_blank_notebook_decodes_to_none() -> None:
    assert notebook.decode(bytes(NOTEBOOK_BYTES)) is None


def written_with(offset: int, value: int) -> bytes:
    raw = bytearray(Notebook(3, 2, 77, 9).encode())
    raw[offset] = value
    return bytes(raw)


@pytest.mark.parametrize(
    "raw",
    [
        written_with(0, 2),
        written_with(3, 1),
        written_with(1, 0),
        bytes.fromhex("01030200") + bytes(NOTEBOOK_BYTES - 4),
        bytes(NOTEBOOK_BYTES - 1),
    ],
    ids=["layout 2", "byte 3", "behavior 0", "dice 0", "63 bytes"],
)
def test_unreadable_notebooks_are_refused(raw: bytes) -> None:
    with pytest.raises(notebook.NotebookError):
        notebook.decode(raw)


def test_in_phase_moves_phase_and_its_start() -> None:
    moved = Notebook(3, 0, 77, 9, SCRATCH).in_phase(4, 1000)
    assert moved == Notebook(3, 4, 77, 1000, SCRATCH)


def test_open_for_starts_a_blank_notebook(view) -> None:
    opened = notebook.open_for(view(random_seed=-3, object_index=4), 3, 1000)
    assert opened == Notebook(3, 0, dice.seed(-3, 4), 1000)


def test_open_for_keeps_the_behaviors_own_notebook(view) -> None:
    kept = Notebook(3, 5, 77, 900, SCRATCH)
    assert notebook.open_for(view(notebook=kept.encode()), 3, 1000) == kept


def test_open_for_restarts_another_behaviors_notebook_keeping_its_dice(view) -> None:
    other = Notebook(8, 5, 77, 900, SCRATCH)
    assert notebook.open_for(view(notebook=other.encode()), 3, 1000) == Notebook(
        3, 0, 77, 1000
    )


def test_open_for_restarts_an_unreadable_notebook(view, caplog) -> None:
    craft = view(notebook=written_with(0, 2), random_seed=-3, object_index=4)
    assert notebook.open_for(craft, 3, 1000) == Notebook(3, 0, dice.seed(-3, 4), 1000)
    assert "notebook.unreadable object=4 behavior=3" in caplog.text


@pytest.mark.parametrize(
    "fields",
    [(0, 0, 1, 0), (256, 0, 1, 0), (1, 256, 1, 0), (1, 0, 0, 0), (1, 0, 1, -1)],
    ids=["behavior 0", "behavior 256", "phase 256", "dice 0", "tick -1"],
)
def test_notebook_fields_out_of_range_are_refused(fields: tuple[int, ...]) -> None:
    with pytest.raises(ValueError):
        Notebook(*fields)

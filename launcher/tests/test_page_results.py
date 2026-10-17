"""The kept debriefings: the newest 20, one file each, read back whole.

Purpose:
    Prove ``ResultStore`` numbers debriefings from 1 up, keeps the newest
    ``keep`` and drops the oldest, writes each file whole, skips a broken
    file without removing it, answers ``results.list`` newest first and
    ``results.get`` with null for one it does not keep, in a folder and in
    memory.

Flow:
    Made-up debriefings with only the fields a summary reads; the commands
    through ``Control.handle``.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_page_results.py``
"""

from __future__ import annotations

import json
import logging

import pytest
from pagedata import check_valid
from pagedata import LAUNCHER_VERSION
from pagedata import request

from jedimaster.page.control import Control
from jedimaster.page.results import KEEP
from jedimaster.page.results import ResultStore
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)


def debrief(result_id: int) -> dict:
    """Return a made-up debriefing holding the fields a summary reads."""
    return {
        "result_id": result_id,
        "ended_at": f"2026-10-16T21:{result_id % 60:02d}:00Z",
        "title": f"Battle {result_id}",
        "outcome": "completed",
        "score": 100 * result_id,
    }


@pytest.fixture(params=["folder", "memory"])
def store(request, tmp_path):
    return ResultStore(tmp_path / "results" if request.param == "folder" else None)


def test_ids_count_from_one_and_the_newest_comes_first(store):
    assert store.next_id() == 1 and store.summaries() == []
    for _ in range(3):
        store.add(debrief(store.next_id()))
    assert [s["result_id"] for s in store.summaries()] == [3, 2, 1]
    assert store.summaries()[0] == debrief(3)
    assert store.get(2) == debrief(2) and store.get(4) is None


def test_the_newest_twenty_are_kept_and_the_oldest_dropped(store):
    for _ in range(KEEP + 3):
        store.add(debrief(store.next_id()))
    kept = [s["result_id"] for s in store.summaries()]
    assert kept == list(range(KEEP + 3, 3, -1))
    assert store.get(3) is None and store.next_id() == KEEP + 4


def test_a_file_is_written_whole_and_named_by_its_id(tmp_path):
    store = ResultStore(tmp_path / "results")
    store.add(debrief(1))
    assert sorted(p.name for p in (tmp_path / "results").iterdir()) == ["1.json"]
    assert json.loads((tmp_path / "results" / "1.json").read_text()) == debrief(1)


def test_a_broken_file_is_skipped_kept_and_warned_about(tmp_path, caplog):
    folder = tmp_path / "results"
    store = ResultStore(folder)
    store.add(debrief(1))
    (folder / "2.json").write_text("{not json", encoding="utf-8")
    (folder / "3.json").write_text(json.dumps(debrief(9)), encoding="utf-8")
    (folder / "notes.json").write_text("{}", encoding="utf-8")
    with caplog.at_level(logging.WARNING):
        assert [s["result_id"] for s in store.summaries()] == [1]
    assert "cannot read the debriefing" in caplog.text
    assert "is not debriefing 3" in caplog.text
    assert (folder / "2.json").exists() and store.next_id() == 4


def test_the_commands_answer_from_the_store(tmp_path):
    store = ResultStore(tmp_path / "results")
    control = Control(
        None, SettingsStore(tmp_path / "s.json"), LAUNCHER_VERSION, results=store
    )
    store.add(debrief(1))
    listed = check_valid(control.handle(request(1, "results.list")).reply)
    assert listed["result"] == {"results": [debrief(1)]}
    missing = check_valid(
        control.handle(request(2, "results.get", {"result_id": 5})).reply
    )
    assert missing["result"] == {"debrief": None}


@pytest.mark.parametrize(
    "args", [{}, {"result_id": 0}, {"result_id": "1"}, {"result_id": 1, "x": 1}]
)
def test_results_get_refuses_bad_arguments(tmp_path, args):
    control = Control(None, SettingsStore(tmp_path / "s.json"), LAUNCHER_VERSION)
    reply = check_valid(control.handle(request(1, "results.get", args)).reply)
    assert reply["ok"] is False and reply["error"]["code"] == "bad_arguments"

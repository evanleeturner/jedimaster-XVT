"""The flown battles' debriefings the launcher keeps: the newest 20.

Purpose:
    Keep each flown battle's debriefing after the battle ends, so a page
    opened later and the bot can show any of the newest ones.

Flow:
    ``ResultStore(folder)`` reads nothing at start; ``add`` writes one
    debriefing as ``<result_id>.json`` in ``folder`` and removes the oldest
    past ``keep``; ``next_id`` gives the number the next one takes;
    ``summaries`` and ``get`` read the folder each time they are asked.
    Without a folder the debriefings are kept in memory only.

Invariants:
    - A result id is a whole number from 1, one more than the newest kept,
      so ids only grow while any debriefing is kept.
    - A file is written whole (a temporary file, then a rename), so a reader
      never sees half of one.
    - A file that is not JSON, not an object, or whose ``result_id`` is not
      its name is skipped with a WARNING and never removed.
    - At most ``keep`` debriefings stay after ``add``.

Call:
    ``store = ResultStore(folder); store.add(debrief); store.summaries()``
"""

from __future__ import annotations

import json
import logging
import os
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

Json = dict[str, Any]

RESULTS_FOLDER = "results"
"""The folder beside the settings file that holds the kept debriefings."""
KEEP = 20
"""How many debriefings the launcher keeps, the newest first (open choice 1)."""
SUMMARY_FIELDS = ("result_id", "ended_at", "title", "outcome", "score")
"""The fields of one line of ``results.list``."""


class ResultStore:
    """The kept debriefings, one file each, or in memory without a folder."""

    def __init__(self, folder: Path | None, keep: int = KEEP) -> None:
        """Keep at most ``keep`` debriefings in ``folder`` (None: in memory)."""
        self.folder = folder
        self.keep = keep
        self._memory: dict[int, Json] = {}

    def _kept(self) -> dict[int, Path | Json]:
        """Return each kept id and its file (or, in memory, the debriefing)."""
        if self.folder is None:
            return dict(self._memory)
        if not self.folder.is_dir():
            return {}
        kept: dict[int, Path | Json] = {}
        for path in self.folder.glob("*.json"):
            if path.stem.isdigit() and int(path.stem) > 0:
                kept[int(path.stem)] = path
        return kept

    def next_id(self) -> int:
        """Return the id the next debriefing takes."""
        return max(self._kept(), default=0) + 1

    def add(self, debrief: Json) -> None:
        """Keep ``debrief`` under its ``result_id``; drop the oldest past ``keep``.

        Raises ``OSError`` when the folder or the file cannot be written.
        """
        result_id = debrief["result_id"]
        if self.folder is None:
            self._memory[result_id] = debrief
        else:
            self.folder.mkdir(parents=True, exist_ok=True)
            path = self.folder / f"{result_id}.json"
            part = path.with_suffix(".part")
            part.write_text(json.dumps(debrief, indent=1), encoding="utf-8")
            os.replace(part, path)
        logger.info("debriefing %d kept: %s", result_id, debrief["title"])
        for old in sorted(self._kept())[: -self.keep]:
            self._drop(old)

    def _drop(self, result_id: int) -> None:
        if self.folder is None:
            del self._memory[result_id]
        else:
            (self.folder / f"{result_id}.json").unlink(missing_ok=True)
        logger.debug("debriefing %d dropped: more than %d kept", result_id, self.keep)

    def get(self, result_id: int) -> Json | None:
        """Return the kept debriefing ``result_id``, or None when there is none."""
        found = self._kept().get(result_id)
        if found is None or isinstance(found, dict):
            return found
        return self._read(result_id, found)

    def _read(self, result_id: int, path: Path) -> Json | None:
        try:
            debrief = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            logger.warning("cannot read the debriefing %s: %s", path, exc)
            return None
        if not isinstance(debrief, dict) or debrief.get("result_id") != result_id:
            logger.warning("the debriefing %s is not debriefing %d", path, result_id)
            return None
        return debrief

    def summaries(self) -> list[Json]:
        """Return one line per readable kept debriefing, the newest first."""
        lines = []
        for result_id in sorted(self._kept(), reverse=True):
            debrief = self.get(result_id)
            if debrief is not None:
                lines.append({name: debrief[name] for name in SUMMARY_FIELDS})
        return lines

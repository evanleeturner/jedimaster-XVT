"""The debriefing a page shows: one flown battle's numbers, each with its name.

Purpose:
    Turn the engine's launch result and the battle the launcher started into
    the debriefing that the ``launch.ended`` push and ``results.get`` carry,
    with every name the 1997 debriefing drew found once, when the battle ends.

Flow:
    ``install_names`` reads the install's ``fronttxt.txt`` (rank and craft
    type names) and the battle's mission file (flight group and team names)
    into a ``Names``; ``build_debrief`` copies the result's numbers and puts a
    name beside each number that has one.

Invariants:
    - Every number is the result's own, never recounted; the page sums the
      totals the 1997 page summed.
    - A rank is entry 154 plus the rating of ``fronttxt.txt`` and a craft
      row's name is entry 21 plus the row, as the 1997 debriefing draws them
      (``mission_debrief.c``); past the file's entries the name is the file's
      own ``No text.``. Without the file a stand-in names the number.
    - A version 2 or 3 result gives no players, no teams and no detail, and
      a local entry of -1.
    - Names are read when the battle ends, so a debriefing kept for later
      shows the names of the install it was flown on.

Call:
    ``build_debrief(7, "2026-10-16T21:30:00Z", battle, result, names)``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
from typing import Any

from ..install import resolve_game_path
from ..lists.files import resolve
from ..lists.game import sequence_game_path
from ..mission import MissionFormatError
from ..mission import read_mission
from ..text.menus import read_front

logger = logging.getLogger(__name__)

Json = dict[str, Any]

FRONT_FILE = "fronttxt.txt"
RANK_ENTRY = 154
"""The ``fronttxt.txt`` entry of rating 0; the debriefing draws this plus the rating."""
CRAFT_ENTRY = 21
"""The ``fronttxt.txt`` entry of craft row 0; the statistics page draws this plus the row."""
BATTLE_FIELDS = ("mission_type", "side", "mission", "title", "pilot")
"""The fields of the ``launch.started`` push a debriefing repeats."""
PLAYER_FIELDS = (
    "entry",
    "name",
    "rating",
    "team",
    "flight_group",
    "score",
    "kills",
    "shared",
    "assists",
    "losses",
    "shots",
    "hits",
    "warheads",
    "warhead_hits",
    "has_left",
)
TEAM_FIELDS = ("team", "completed", "score", "kills", "shared", "losses", "seconds")
DETAIL_COUNTS = (
    "shots",
    "hits",
    "warheads",
    "warhead_hits",
    "losses",
    "losses_to_collisions",
    "losses_to_starships",
    "losses_to_mines",
    "friendlies_killed",
    "special_inspected",
)
RANK_TABLES = (
    "on_pilot_ranks",
    "on_ai_ranks",
    "killed_by_pilot_ranks",
    "killed_by_ai_ranks",
)
"""The detail's tables whose rows the page only sums, copied as they are."""


@dataclass(frozen=True)
class Names:
    """The names a debriefing puts beside its numbers; empty when not found."""

    front: list[str] | None = None
    flight_groups: list[str] = field(default_factory=list)
    teams: list[str] = field(default_factory=list)

    def rank(self, rating: int) -> str:
        """Return the rank name of ``rating``."""
        if self.front is None:
            return f"Rank {rating}"
        return _entry(self.front, RANK_ENTRY + rating)

    def craft(self, row: int) -> str:
        """Return the name of craft row ``row`` of the kills by craft type."""
        if self.front is None:
            return f"Craft {row}"
        return _entry(self.front, CRAFT_ENTRY + row)

    def flight_group(self, index: int) -> str:
        """Return the mission's name of flight group ``index``."""
        if 0 <= index < len(self.flight_groups) and self.flight_groups[index]:
            return self.flight_groups[index]
        return f"Flight group {index + 1}"

    def team(self, index: int) -> str:
        """Return the mission's name of team ``index``."""
        if 0 <= index < len(self.teams) and self.teams[index]:
            return self.teams[index]
        return f"Team {index + 1}"


def _entry(front: list[str], number: int) -> str:
    """Return entry ``number`` as the game reads it: ``No text.`` outside the file."""
    if 0 <= number < len(front):
        return front[number]
    return "No text."


def install_names(install: Path | None, mission_type: str, mission: str) -> Names:
    """Return the names of the battle flown on ``mission`` from ``install``.

    Each source that is missing or unreadable is left empty with a WARNING,
    so the debriefing still carries every number. Never raises.
    """
    if install is None:
        logger.debug("no install: the debriefing names numbers only")
        return Names()
    front = None
    path = resolve(install, FRONT_FILE)
    if path is not None and path.is_file():
        try:
            front = [e.decode("iso-8859-1") for e in read_front(path).entries]
        except OSError as exc:
            logger.warning("cannot read %s: %s", path, exc)
    else:
        logger.warning("%s not found: the debriefing names ranks by number", FRONT_FILE)
    groups: list[str] = []
    teams: list[str] = []
    try:
        found = resolve_game_path(install, sequence_game_path(mission_type, mission))
        if found is None or not found.is_file():
            logger.warning("mission %s not found: groups and teams by number", mission)
        else:
            read = read_mission(found)
            groups = [g.name for g in read.flight_groups]
            teams = [t.name for t in read.teams]
    except (MissionFormatError, OSError, ValueError) as exc:
        logger.warning("cannot read mission %s: %s", mission, exc)
    logger.debug(
        "debriefing names: front %s, %d groups, %d teams",
        front is not None,
        len(groups),
        len(teams),
    )
    return Names(front, groups, teams)


def build_debrief(
    result_id: int, ended_at: str, battle: Json, result: Json, names: Names
) -> Json:
    """Return the debriefing of the flown battle ``battle`` whose result is ``result``.

    ``battle`` holds the ``launch.started`` push's fields; ``result`` is a
    checked result of version 2, 3 or 4. ``ended_at`` is the UTC time the
    battle ended, ``YYYY-MM-DDTHH:MM:SSZ``. Raises ``KeyError`` for a field
    missing from ``battle`` or ``result``.
    """
    debrief: Json = {"result_id": result_id, "ended_at": ended_at}
    debrief.update({name: battle[name] for name in BATTLE_FIELDS})
    for name in ("outcome", "score", "kills", "losses", "goals_met_seconds"):
        debrief[name] = result[name]
    if result["schema_version"] < 4:
        debrief.update(local_entry=-1, players=[], teams=[], detail=None)
        logger.debug(
            "debriefing %d: a version %d result", result_id, result["schema_version"]
        )
        return debrief
    players = [_player(p, names) for p in result["players"]]
    by_entry = {p["entry"]: p["name"] for p in players}
    debrief["local_entry"] = result["local_entry"]
    debrief["players"] = players
    debrief["teams"] = [
        {**{n: t[n] for n in TEAM_FIELDS}, "name": names.team(t["team"])}
        for t in result["teams"]
    ]
    debrief["detail"] = _detail(result["detail"], names, by_entry)
    logger.debug(
        "debriefing %d: %d players, %d teams, local entry %d",
        result_id,
        len(players),
        len(debrief["teams"]),
        debrief["local_entry"],
    )
    return debrief


def _player(player: Json, names: Names) -> Json:
    """Return one player's row with its rank and flight group named."""
    row = {n: player[n] for n in PLAYER_FIELDS}
    row["rank"] = names.rank(player["rating"])
    row["flight_group_name"] = names.flight_group(player["flight_group"])
    return row


def _detail(detail: Json, names: Names, by_entry: dict[int, str]) -> Json:
    """Return the local pilot's tables, each row that has a name given it."""
    out: Json = {n: detail[n] for n in DETAIL_COUNTS}
    out["by_craft"] = [
        {**row, "name": names.craft(row["craft_type"])} for row in detail["by_craft"]
    ]
    for table in RANK_TABLES:
        out[table] = [dict(row) for row in detail[table]]
    for table in ("on_players", "from_players"):
        out[table] = [
            {**row, "name": by_entry.get(row["entry"], f"Player {row['entry'] + 1}")}
            for row in detail[table]
        ]
    for table in ("on_flight_groups", "from_flight_groups"):
        out[table] = [
            {**row, "name": names.flight_group(row["flight_group"])}
            for row in detail[table]
        ]
    return out

"""The debriefing of a flown battle: every number copied, each name found.

Purpose:
    Prove ``build_debrief`` copies the result's numbers unchanged and names
    them as the 1997 debriefing does (rank 154 plus the rating, craft 21 plus
    the row, the mission's flight groups and teams), and that
    ``install_names`` falls back to stand-ins without ever raising.

Flow:
    Made-up results shaped like a two-player melee; a made-up install from
    ``briefingdata`` with one melee mission written here.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_page_debrief.py``
"""

from __future__ import annotations

import logging

import pytest
from briefingdata import make_install
from briefingdata import mission_bytes
from pagedata import check_valid

from jedimaster.page.debrief import build_debrief
from jedimaster.page.debrief import install_names
from jedimaster.page.debrief import Names

logger = logging.getLogger(__name__)

BATTLE = {
    "mission_type": "melee",
    "side": "none",
    "mission": "duel.tie",
    "title": "Duel",
    "pilot": "Wedge",
}
ENDED = "2026-10-16T21:30:00Z"


def player(entry: int, name: str, rating: int, team: int, group: int, **counts) -> dict:
    """Return one version 4 player row; every count not given is 0."""
    row = dict(entry=entry, slot=entry, name=name, team=team, flight_group=group)
    row.update(rating=rating, has_left=False)
    for count in ("score", "kills", "shared", "assists", "losses", "shots", "hits"):
        row[count] = counts.get(count, 0)
    for count in ("warheads", "warhead_hits"):
        row[count] = counts.get(count, 0)
    return row


def detail(**tables) -> dict:
    """Return a version 4 detail whose counts are 1 to 10 and tables are empty unless given."""
    counts = (
        "shots hits warheads warhead_hits losses losses_to_collisions "
        "losses_to_starships losses_to_mines friendlies_killed special_inspected"
    ).split()
    out: dict = {name: n for n, name in enumerate(counts, 1)}
    for table in (
        "by_craft on_pilot_ranks on_ai_ranks killed_by_pilot_ranks killed_by_ai_ranks "
        "on_players from_players on_flight_groups from_flight_groups"
    ).split():
        out[table] = tables.get(table, [])
    return out


def result_v4(**changes) -> dict:
    """Return a flown version 4 result of a two-player melee, with ``changes``."""
    result = {
        "schema_version": 4,
        "status": "flown",
        "exit_code": 0,
        "outcome": "completed",
        "score": 1450,
        "kills": 5,
        "losses": 1,
        "goals_met_seconds": 412,
        "why": "",
        "local_entry": 0,
        "players": [
            player(0, "Wedge", 2, 0, 0, score=1450, kills=5, shared=1, shots=212),
            player(1, "Vader", 6, 1, 1, score=980, kills=3, losses=4),
        ],
        "teams": [
            dict(
                team=0,
                completed=True,
                score=1450,
                kills=5,
                shared=1,
                losses=1,
                seconds=412,
            ),
            dict(
                team=1,
                completed=False,
                score=980,
                kills=3,
                shared=0,
                losses=4,
                seconds=0,
            ),
        ],
        "detail": detail(
            by_craft=[dict(craft_type=5, kills=3, shared=1, assists=1)],
            on_ai_ranks=[dict(rank=1, kills=2, shared=1, assists=2)],
            killed_by_ai_ranks=[dict(rank=2, times=1)],
            on_players=[dict(entry=1, kills=2, shared=0)],
            from_players=[dict(entry=6, kills=1, shared=0)],
            on_flight_groups=[dict(flight_group=2, kills=3, shared=1)],
        ),
    }
    result.update(changes)
    return result


def front() -> list[str]:
    """Return front text whose entry n reads ``line n``, with the names the tests read."""
    lines = [f"line {n}" for n in range(200)]
    lines[156], lines[160], lines[26] = "Trainee", "Of 2nd", "TIE Fighter"
    return lines


NAMES = Names(front(), ["Red", "Blue", "Gold Tower"], ["Red Team", "Blue Team"])


def test_every_number_is_the_results_own():
    result = result_v4()
    debrief = build_debrief(3, ENDED, BATTLE, result, NAMES)
    assert debrief["result_id"] == 3 and debrief["ended_at"] == ENDED
    assert {k: debrief[k] for k in BATTLE} == BATTLE
    for key in (
        "outcome",
        "score",
        "kills",
        "losses",
        "goals_met_seconds",
        "local_entry",
    ):
        assert debrief[key] == result[key], key
    for got, given in zip(debrief["players"], result["players"], strict=True):
        assert {k: got[k] for k in given if k != "slot"} == {
            k: v for k, v in given.items() if k != "slot"
        }
    for got, given in zip(debrief["teams"], result["teams"], strict=True):
        assert {k: got[k] for k in given} == given
    for key, value in result["detail"].items():
        if isinstance(value, int):
            assert debrief["detail"][key] == value, key
    check_valid({"id": 1, "ok": True, "result": {"debrief": debrief}})


def test_each_number_gets_the_name_the_1997_debriefing_draws():
    debrief = build_debrief(1, ENDED, BATTLE, result_v4(), NAMES)
    wedge, vader = debrief["players"]
    assert (wedge["rank"], wedge["flight_group_name"]) == ("Trainee", "Red")
    assert (vader["rank"], vader["flight_group_name"]) == ("Of 2nd", "Blue")
    assert [t["name"] for t in debrief["teams"]] == ["Red Team", "Blue Team"]
    tables = debrief["detail"]
    assert tables["by_craft"][0]["name"] == "TIE Fighter"
    assert tables["on_players"][0]["name"] == "Vader"
    assert tables["from_players"][0]["name"] == "Player 7", "no such player: a stand-in"
    assert tables["on_flight_groups"][0]["name"] == "Gold Tower"
    assert tables["on_ai_ranks"] == [dict(rank=1, kills=2, shared=1, assists=2)]
    assert "slot" not in wedge


def test_a_rank_past_the_front_text_is_the_games_no_text():
    assert Names(front()).rank(200 - 154) == "No text."
    assert Names(front()).rank(-200) == "No text."


def test_without_names_every_number_has_a_stand_in():
    names = Names()
    assert (names.rank(2), names.craft(5)) == ("Rank 2", "Craft 5")
    assert (names.flight_group(0), names.team(1)) == ("Flight group 1", "Team 2")
    assert Names(front(), ["", "Blue"], [""]).flight_group(0) == "Flight group 1"
    assert Names(front(), [], [""]).team(0) == "Team 1"


@pytest.mark.parametrize("version", [2, 3])
def test_an_older_result_gives_no_tables(version):
    old = {
        k: v
        for k, v in result_v4().items()
        if k not in ("local_entry", "players", "teams", "detail")
    }
    debrief = build_debrief(2, ENDED, BATTLE, dict(old, schema_version=version), NAMES)
    assert (debrief["local_entry"], debrief["players"], debrief["teams"]) == (
        -1,
        [],
        [],
    )
    assert debrief["detail"] is None and debrief["score"] == 1450
    check_valid({"id": 1, "ok": True, "result": {"debrief": debrief}})


def test_a_missing_field_raises_key_error():
    result = result_v4()
    del result["teams"]
    with pytest.raises(KeyError):
        build_debrief(1, ENDED, BATTLE, result, NAMES)


def test_names_come_from_the_install_front_text_and_the_mission(tmp_path):
    install = make_install(tmp_path)
    groups = [{"name": "Red"}, {"name": "Blue"}]
    (install / "Melee").mkdir()
    (install / "Melee" / "DUEL.TIE").write_bytes(
        mission_bytes(groups, team_names=["Red Team", "Blue Team"])
    )
    names = install_names(install, "melee", "duel.tie")
    assert (names.rank(2), names.craft(5)) == ("line 156", "line 26")
    assert names.flight_groups == ["Red", "Blue"]
    assert names.teams[:2] == ["Red Team", "Blue Team"]


def test_a_missing_mission_or_front_text_is_a_warning_not_an_error(tmp_path, caplog):
    install = make_install(tmp_path)
    (install / "FRONTTXT.TXT").unlink()
    with caplog.at_level(logging.WARNING):
        names = install_names(install, "melee", "gone.tie")
    assert names == Names()
    assert "fronttxt.txt not found" in caplog.text
    assert "mission gone.tie not found" in caplog.text
    assert install_names(None, "melee", "gone.tie") == Names()

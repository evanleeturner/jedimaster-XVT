"""Build what the launch tests need: solo lists, a pilot folder and a stand-in engine.

Purpose:
    Give the launch, battle, walk and door tests one made-up install with
    solo lists, one pilot folder and one stand-in for the game program, so
    a launch can be flown end to end without the game.

Flow:
    ``make_solo_install`` adds solo lists to ``pagedata.make_install``;
    ``make_pilots`` writes pilot files; ``make_engine`` writes a script that
    acts as the engine (``--launch FILE``) and behaves as its first extra
    argument says; ``make_control`` joins them.

Invariants:
    - No game data: every title, file and pilot name is a generic word.
    - The stand-in engine and AI program answer ``--version`` as the real ones
      do (``ai_wire=11``); the AI program sits where a package keeps it.
    - The stand-in engine's behaviours (``#BEHAVE#`` is filled by ``make_engine``): ``done`` (writes a flown result,
      exits 0), ``done4`` (the same as version 4, one player), ``refuse`` (a refused result, exit 3), ``mismatch`` (a flown
      result, exit 5), ``silent`` (exits 0 without a result), ``hang``
      (sleeps), ``stubborn`` (sleeps and ignores SIGTERM), ``crash``
      (exits 9 without a result). ``--copy=PATH`` saves the description.

Call:
    ``control, flight = make_control(tmp_path, "done")``
"""

from __future__ import annotations

import json
import logging
import stat
import sys
from pathlib import Path

from listdata import crlf
from pagedata import LAUNCHER_VERSION
from pagedata import make_install

from jedimaster.page.battles import Flight
from jedimaster.page.control import Control
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)

PLAYER = "7001"
PILOTS = ("Ace", "Bravo Two", "Cee")

ENGINE = f"""#!{sys.executable}
import json, signal, sys, time
args = sys.argv[1:]
if args == ["--version"]:
#VERSION#
path = args[args.index("--launch") + 1]
#BEHAVE#
description = json.load(open(path))
for a in args:
    if a.startswith("--copy="):
        json.dump(description, open(a[7:], "w"))
RESULT = dict(schema_version=2, status="flown", exit_code=0, outcome="completed",
              score=10, kills=2, losses=0, goals_met_seconds=61, why="")
def write(**changes):
    json.dump(dict(RESULT, **changes), open(description["result"], "w"))
if behave == "done":
    write()
elif behave == "done4":
    COUNTS = dict(shots=40, hits=12, warheads=2, warhead_hits=1, losses=0,
                  losses_to_collisions=0, losses_to_starships=0, losses_to_mines=0,
                  friendlies_killed=0, special_inspected=0)
    LISTS = ("on_pilot_ranks", "on_ai_ranks", "killed_by_pilot_ranks",
             "killed_by_ai_ranks", "on_players", "from_players", "from_flight_groups")
    write(schema_version=4,
          sith=dict(setting="none", flight_group=-1, flight_name="", craft_type=-1,
                    apprentices_flown=0, apprentices_lost=0, lord_flown=0,
                    lord_lost=0, why=""),
          local_entry=0,
          players=[dict(entry=0, slot=0, name="Ace", team=0, flight_group=0,
                        rating=2, score=10, kills=2, shared=0, assists=1, losses=0,
                        shots=40, hits=12, warheads=2, warhead_hits=1,
                        has_left=False)],
          teams=[dict(team=0, completed=True, score=10, kills=2, shared=0,
                      losses=0, seconds=61)],
          detail=dict(COUNTS, by_craft=[dict(craft_type=1, kills=2, shared=0,
                                             assists=1)],
                      on_flight_groups=[dict(flight_group=0, kills=2, shared=0)],
                      **dict.fromkeys(LISTS, [])))
elif behave == "refuse":
    write(status="refused", exit_code=3, outcome="none", score=0, kills=0,
          losses=0, goals_met_seconds=0, why="The mission is not open.")
    sys.exit(3)
elif behave == "mismatch":
    write()
    sys.exit(5)
elif behave == "crash":
    sys.exit(9)
elif behave in ("hang", "stubborn"):
    if behave == "stubborn":
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
    open(description["result"] + ".started", "w").write("up")
    time.sleep(60)
"""


WIRE = 11
"""The AI version both stand-ins speak unless a test says otherwise."""

SAYS_WIRE = """    print("OpenXvT stand-in")
    print("ai_wire=#WIRE#")
    sys.exit(0)"""
SAYS_NOTHING = """    sys.stderr.write("OpenXvT: unknown argument: --version\\n")
    sys.exit(2)"""

AI_PROGRAM = """import sys
if sys.argv[1:] == ["--version"]:
#SAYS#
"""


def make_ai_program(
    folder: Path, wire: int | None = WIRE, says: str | None = None
) -> Path:
    """Write a stand-in AI program into ``folder``; return ``folder``.

    It answers ``--version`` with ``ai_wire=<wire>``; ``wire`` None makes it
    exit 2 with a complaint, and ``says`` replaces the whole reply code.
    """
    folder.mkdir(parents=True, exist_ok=True)
    if says is None:
        says = (
            f'    print("ai_wire={wire}")\n    sys.exit(0)'
            if wire is not None
            else '    sys.stderr.write("unknown argument\\n")\n    sys.exit(2)'
        )
    (folder / "__main__.py").write_text(
        AI_PROGRAM.replace("#SAYS#", says), encoding="utf-8"
    )
    return folder


def make_solo_install(root: Path) -> Path:
    """Return a made-up install that also has the solo lists."""
    return add_solo_lists(make_install(root))


def add_solo_lists(install: Path) -> Path:
    """Write the solo lists into ``install``; return it. The network menus stay."""
    files = {
        "Train/REBEL.LST": crlf(
            "[Basic]", "1", "ALPHA.TIE", "First Flight",
            "2", "& BRAVO.TIE", "Second Flight",
            "[Advanced]", "3", "* 1 2 CHARLIE.TIE", "Third Flight",
            "4", "bad name.tie", "Odd Flight",
        ),
        "Train/IMPERIAL.LST": crlf("1", "ECHO.TIE", "Imperial Flight"),
        "Tourn/MISSION.LST": crlf("1", "FIRST.TOU", "First Tournament"),
        "Campaign/REBEL.LST": crlf("1", "CAMP.CMP", "Rebel Campaign"),
    }  # fmt: skip
    for rel, data in files.items():
        path = install / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return install


def make_pilots(root: Path, names: tuple[str, ...] = PILOTS) -> Path:
    """Write one pilot file per name in a folder under ``root``; return the folder."""
    folder = root / "pilots"
    folder.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(names):
        record = name.encode("latin-1").ljust(14, b"\0") + b"rest of the record"
        (folder / f"pilot{index}.plt").write_bytes(record)
    return folder


BEHAVE_FROM_ARGS = """behave = next((a for a in args if a in
    ("done", "done4", "refuse", "mismatch", "silent", "hang", "stubborn",
     "crash")), "done")"""


def make_engine(
    root: Path,
    behave_file: Path | None = None,
    wire: int | None = WIRE,
    packaged: bool = True,
) -> Path:
    """Write the stand-in engine script; return its path.

    The behaviour is the first extra argument that names one, or, when
    ``behave_file`` is given, the word that file holds each time the engine runs.
    ``--version`` says ``ai_wire=<wire>``, or is refused as an old build's is
    when ``wire`` is None. With ``packaged`` the stand-in AI program is written
    where a package keeps it, ``root/ai/jedimaster_ai``, speaking ``WIRE``.
    """
    path = root / "engine.py"
    line = BEHAVE_FROM_ARGS
    if behave_file is not None:
        line = f"behave = open({str(behave_file)!r}).read().strip()"
    version = (
        SAYS_WIRE.replace("#WIRE#", str(wire)) if wire is not None else SAYS_NOTHING
    )
    text = ENGINE.replace("#BEHAVE#", line).replace("#VERSION#", version)
    if packaged:
        make_ai_program(root / "ai" / "jedimaster_ai")
    path.write_text(text, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    return path


CONTROLS: list[Control] = []
"""Every control ``make_control`` made in this test, for ``conftest`` to settle."""


def make_control(
    root: Path,
    behave: str = "done",
    *extra: str,
    timeout: float = 30.0,
    ai_program: str | None = None,
) -> tuple[Control, Flight]:
    """Return a control over the solo install with the stand-in engine, and its flight.

    One page is reported open. ``behave`` and ``extra`` are the engine's
    extra arguments; ``ai_program`` is the flight's folder (None: the one packaged beside the engine).
    """
    store = SettingsStore(root / "config" / "settings.json")
    flight = Flight(
        str(make_engine(root)),
        (behave, *extra),
        make_pilots(root),
        timeout,
        ai_program=ai_program,
    )
    control = Control(make_solo_install(root), store, LAUNCHER_VERSION, flight)
    control.pages_open = lambda: 1
    CONTROLS.append(control)
    return control, flight


def solo_args(**changes: str) -> dict[str, str]:
    """Return the arguments of a good ``launch.solo`` request, with ``changes`` applied."""
    args = {
        "player": PLAYER,
        "mission_type": "training",
        "side": "rebel",
        "mission": "alpha.tie",
        "pilot": "Ace",
    }
    args.update(changes)
    return args


def result_of(reply: dict) -> dict:
    """Return the result of an ok reply."""
    assert reply["ok"] is True, json.dumps(reply)
    return reply["result"]

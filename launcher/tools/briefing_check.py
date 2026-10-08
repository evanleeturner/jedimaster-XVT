"""Check every stock briefing's events by structure and count event types.

Purpose:
    The answer sheets do not print briefing events. This tool checks all 8
    briefings of every listed mission against the document instead (End
    Briefing 0x22 at 9999 closes the list; EventsLength matches; each
    event's variable count matches its type) and counts event types.

Flow:
    Read each mission, run ``check_briefing`` and ``start_events_rule`` on
    each briefing, tally findings, event types (hex), StartEvents readings
    and briefings with leftover SHORTs after End Briefing; print the tallies.

Invariants:
    - Exit status 0 when no briefing has a structural finding, 1 otherwise.
    - Types the document's EventType list does not name are listed by
      themselves.

Call:
    ``python tools/briefing_check.py --missions FILE [--install DIR]``; the
    mission list lives on the machine that runs the check
"""

from __future__ import annotations

import argparse
import collections
import logging
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from jedimaster.install import find_install  # noqa: E402
from jedimaster.mission import read_mission  # noqa: E402
from jedimaster.mission.checks import check_briefing  # noqa: E402
from jedimaster.mission.checks import start_events_rule  # noqa: E402
from jedimaster.mission.names import EVENTTYPE  # noqa: E402

logger = logging.getLogger("briefing_check")


def main(argv: list[str] | None = None) -> int:
    """Run the checks; return 0 when every briefing passes."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--install", help="install folder (default: found)")
    parser.add_argument(
        "--missions", required=True, help="the mission list file (local only)"
    )
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.WARNING)
    install = find_install(args.install) if args.install else find_install()
    if install is None:
        print("no install found")
        return 1
    missions = [
        m.strip()
        for m in Path(args.missions).read_text(encoding="utf-8").split("\n")
        if m.strip()
    ]
    findings: collections.Counter[str] = collections.Counter()
    types: collections.Counter[int] = collections.Counter()
    rules: collections.Counter[str] = collections.Counter()
    tails = 0
    briefings = 0
    failing = []
    for rel in missions:
        mission = read_mission(install / rel)
        for index, briefing in enumerate(mission.briefings):
            briefings += 1
            problems = check_briefing(briefing, len(mission.flight_groups))
            findings.update(problems)
            if problems:
                failing.append(f"{rel} briefing {index}: {','.join(problems)}")
            rules[start_events_rule(briefing)] += 1
            types.update(event.type for event in briefing.events)
            tails += bool(briefing.events_tail)
    events = sum(types.values())
    print(f"missions {len(missions)}, briefings {briefings}, events {events}")
    print(f"structural findings: {dict(findings) or 'none'}")
    for line in failing:
        print("  " + line)
    print(f"StartEvents readings: {dict(sorted(rules.items()))}")
    print(f"briefings with leftover SHORTs after End Briefing: {tails}")
    print("event types (hex type, document name, count):")
    for kind, count in sorted(types.items()):
        print(f"  0x{kind:02X} {EVENTTYPE.get(kind, '(not named)'):<16} {count}")
    unnamed = sorted(k for k in types if k not in EVENTTYPE)
    print(
        "types the document does not name: %s"
        % (", ".join(f"0x{k:02X}" for k in unnamed) or "none")
    )
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())

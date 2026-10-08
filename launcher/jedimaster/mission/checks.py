"""Structural checks of a briefing's event list against the document.

Purpose:
    The answer sheets do not print briefing events, so the events are checked
    by structure instead: the list ends with End Briefing (0x22) at time
    9999, its length agrees with the briefing's own counts, and every event
    carries the number of variables the document's EventType list gives its
    type.

Flow:
    ``check_briefing`` re-walks the events the reader kept and returns a list
    of findings (empty when the briefing passes). ``start_events_rule`` says
    which reading of StartEvents a briefing satisfies.

Invariants:
    - A check never raises on odd data; every problem becomes a finding.
    - Findings are short stable strings, so a tool can count them.

Call:
    ``problems = check_briefing(mission.briefings[0])``
"""

from __future__ import annotations

import logging

from .model import Briefing
from .model import Event
from .reader import END_BRIEFING
from .reader import EVENT_ARG_COUNTS

logger = logging.getLogger(__name__)

END_TIME = 9999
STRING_SLOTS = 32
STRING_EVENTS = (0x04, 0x05)
FG_TAG_EVENTS = tuple(range(0x09, 0x11))
TEXT_TAG_EVENTS = tuple(range(0x12, 0x1A))


def _shorts(briefing: Briefing) -> int:
    return sum(2 + len(e.variables) for e in briefing.events)


def check_briefing(briefing: Briefing, num_fgs: int | None = None) -> list[str]:
    """Return the structural problems of one briefing's events, [] if none.

    Findings: ``no-end`` (the walk did not reach End Briefing),
    ``end-time`` (End Briefing not at 9999), ``unknown-type`` (a type the
    document does not list), ``arg-count`` (an event whose variable count
    differs from the document's for its type), ``length`` (EventsLength is
    not the SHORT count up to and including End Briefing), ``time-order``
    (an event earlier than the one before it), ``bad-reference`` (a String#
    or Tag# outside the 32 strings/tags, or, when ``num_fgs`` is given, an FG
    tag naming no flight group). The last two catch a walk that a wrong
    argument count has knocked out of step. Does not judge StartEvents (see
    ``start_events_rule``) or the leftover SHORTs after End Briefing.
    """
    findings: list[str] = []
    if not briefing.events_complete or not briefing.events:
        findings.append("no-end")
    else:
        last = briefing.events[-1]
        if last.type != END_BRIEFING:
            findings.append("no-end")
        elif last.time != END_TIME:
            findings.append("end-time")
    for event in briefing.events:
        expected = EVENT_ARG_COUNTS.get(event.type)
        if expected is None:
            findings.append("unknown-type")
        elif expected != len(event.variables):
            findings.append("arg-count")
    if _shorts(briefing) != briefing.events_length:
        findings.append("length")
    times = [event.time for event in briefing.events]
    if any(later < earlier for earlier, later in zip(times, times[1:], strict=False)):
        findings.append("time-order")
    if any(not _reference_ok(event, num_fgs) for event in briefing.events):
        findings.append("bad-reference")
    return findings


def _reference_ok(event: Event, num_fgs: int | None) -> bool:
    if not event.variables:
        return True
    first = event.variables[0]
    if event.type in STRING_EVENTS or event.type in TEXT_TAG_EVENTS:
        return 0 <= first < STRING_SLOTS
    if event.type in FG_TAG_EVENTS and num_fgs is not None:
        return 0 <= first < num_fgs
    return True


def start_events_rule(briefing: Briefing) -> str:
    """Return which reading of StartEvents this briefing satisfies.

    ``"time-zero"``: StartEvents is the SHORT count of the events at time 0
    (the document's reading) and also the position of the first event at or
    after CurrentTime. ``"current-time"``: only the second reading holds.
    ``"time-zero-only"``: only the first holds. ``"neither"``: neither
    holds. Does not check the event list itself.
    """
    zero = 0
    position = 0
    first_at_current = None
    for event in briefing.events:
        if first_at_current is None and event.time >= briefing.current_time:
            first_at_current = position
        if event.time == 0 and event.type != END_BRIEFING:
            zero += 2 + len(event.variables)
        position += 2 + len(event.variables)
    by_zero = zero == briefing.start_events
    by_current = first_at_current == briefing.start_events
    if by_zero and by_current:
        return "time-zero"
    if by_current:
        return "current-time"
    if by_zero:
        return "time-zero-only"
    return "neither"

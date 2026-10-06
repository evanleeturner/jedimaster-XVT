#!/usr/bin/env python3
"""Check that the log catalog and the log call sites agree.

Reads src/xvt_runtime/log/events.json and every .c and .h file under
src/xvt, src/xvt_runtime, src/xvt_remaster and src/xvt_app, then reports
each place where the two disagree. In src/xvt, the 1997 game's code, the
calls sit in the modern build's arms; the text is read whole, so a call in
any arm counts:

  uncataloged  a call site names an event the catalog does not hold
  orphan       a catalog entry no call site uses
  level        a call site's macro level differs from the catalog's
  fields       a call site writes a field the catalog does not declare, or
               the catalog declares a field no call site writes
  shape        a format string is not "event key=value ...": the event id is
               not a literal two-part dotted name, a word is not key=value,
               or a text value (%s) is not double-quoted
  bypass       a call to Aeron_Log*, xvt_log_write or xvt_crash_note_writef
               outside the files that define the macros
  catalog      a catalog entry is malformed, or its text names a field the
               entry does not declare

XVT_LOG_DEBUG, XVT_LOG_INFO, XVT_LOG_WARN and XVT_LOG_ERROR write levels D,
I, W and E; XVT_LOG_CRASH, the crash note's macro, writes level C. The log
header, its source file, the both-builds header (the macros' empty stand-ins
for the original build) and the crash note's header are not scanned: they
define the macros. The crash note's source file is scanned, since it holds
the XVT_LOG_CRASH call sites, but may name xvt_crash_note_writef: it defines it.
One finding per line, "FAIL <kind> <file>:<line> <detail>", then a summary.
Exit status 0 when nothing is reported, 1 when something is, 2 when the
catalog cannot be read.

    python3 tools/log_catalog_check.py [REPOSITORY_ROOT]
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

CATALOG = Path("src/xvt_runtime/log/events.json")
LOG_HEADER = Path("src/xvt_runtime/log/log.h")
LOG_SOURCE = Path("src/xvt_runtime/log/log.c")
CRASH_HEADER = Path("src/xvt_app/crash_note.h")
CRASH_SOURCE = Path("src/xvt_app/crash_note.c")
SCANNED = ("src/xvt", "src/xvt_runtime", "src/xvt_remaster", "src/xvt_app")

LEVEL_LETTERS = {"DEBUG": "D", "INFO": "I", "WARN": "W", "ERROR": "E", "CRASH": "C"}

MACRO_CALL = re.compile(r"\bXVT_LOG_(DEBUG|INFO|WARN|ERROR|CRASH)\s*\(")
STRING_LITERALS = re.compile(r'\s*(?:"(?:[^"\\]|\\.)*"\s*)+')
ONE_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
BYPASS = re.compile(
    r"\b(?:Aeron_Log(?:Trace|Verbose|Debug|Info|Warn|Error|Critical|Message|MessageV)"
    r"|xvt_log_write|xvt_crash_note_writef|XVT_LOG_AT)\s*\("
)
EVENT_ID = re.compile(r"^[a-z][a-z0-9]*\.[a-z][a-z0-9_]*$")
FIELD = re.compile(r"^([a-z][a-z0-9_]*)=(.+)$")
CONVERSION = re.compile(
    r"^%[-+ 0#]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGaAcp]$"
)
QUOTED = re.compile(r'^"[^"]*"$')
BARE = re.compile(r"^[A-Za-z0-9_.+:/-]+$")
TEXT_CONVERSION = re.compile(r"%[-+ 0#]*(?:\d+|\*)?(?:\.(?:\d+|\*))?[sc]")
TEMPLATE_FIELD = re.compile(r"\{([^{}]*)\}")


class Finding:
    """One disagreement: its kind, where it is, and what it says."""

    def __init__(self, kind: str, where: str, detail: str) -> None:
        self.kind = kind
        self.where = where
        self.detail = detail

    def __str__(self) -> str:
        return f"FAIL {self.kind} {self.where} {self.detail}"


class Site:
    """One macro call: file, line, level letter, event id and field keys."""

    def __init__(self, where: str, level: str, event: str, keys: list[str]) -> None:
        self.where = where
        self.level = level
        self.event = event
        self.keys = keys


def blank_comments(text: str, blank_strings: bool = False) -> str:
    """Return text with every comment replaced by spaces, newlines kept.

    String and character literals are copied through, so a comment marker
    inside one does not start a comment; with blank_strings their contents
    become spaces too, leaving only the quotes, so a macro name inside a
    string is not a call. Offsets and line numbers do not move.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        two = text[i : i + 2]
        if two == "//":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif two == "/*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.extend("\n" if ch == "\n" else " " for ch in text[i:end])
            i = end
        elif c in "\"'":
            quote = c
            out.append(c)
            i += 1
            while i < n and text[i] != quote and text[i] != "\n":
                step = 2 if text[i] == "\\" and i + 1 < n else 1
                piece = text[i : i + step]
                out.append(" " * step if blank_strings else piece)
                i += step
            if i < n and text[i] == quote:
                out.append(quote)
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def unescape(literal: str) -> str:
    """Return the text of a C string literal body with \\" and \\\\ resolved."""
    return literal.replace('\\"', '"').replace("\\\\", "\\")


def parse_format(fmt: str) -> tuple[str | None, list[str], list[str]]:
    """Split a format string into event id, field keys and shape problems.

    The first word is the event id; every later word must be key=value with
    a value that is a printf conversion, a double-quoted text, or a bare
    word. A %s or %c outside quotes is a problem. Returns (event or None,
    keys, problems); event is None when the first word is not a valid id.
    """
    problems: list[str] = []
    if not fmt or fmt != fmt.strip() or "  " in fmt:
        problems.append("words are separated by single spaces, no edge spaces")
    words = [w for w in fmt.split(" ") if w]
    if not words:
        return None, [], problems or ["empty format string"]
    event = words[0]
    if not EVENT_ID.match(event):
        problems.append(f"event id {event!r} is not component.action")
        event = None
    keys: list[str] = []
    for word in words[1:]:
        match = FIELD.match(word)
        if not match:
            problems.append(f"{word!r} is not key=value")
            continue
        key, value = match.group(1), match.group(2)
        if key in keys:
            problems.append(f"field {key!r} repeats")
        keys.append(key)
        if QUOTED.match(value):
            continue
        if TEXT_CONVERSION.search(value):
            problems.append(f'{word!r}: a text value is written key="%s"')
        elif not (CONVERSION.match(value) or ("%" not in value and BARE.match(value))):
            problems.append(
                f"{word!r}: value is not a conversion, quoted text or a bare word"
            )
    return event, keys, problems


def line_of(text: str, offset: int) -> int:
    """Return the 1-based line number of offset in text."""
    return text.count("\n", 0, offset) + 1


def scan_file(path: Path, root: Path) -> tuple[list[Site], list[Finding]]:
    """Return the macro call sites in one file and the findings they raise."""
    text = path.read_text(encoding="utf-8", errors="replace")
    code = blank_comments(text)
    mask = blank_comments(text, blank_strings=True)
    relative = path.relative_to(root).as_posix()
    sites: list[Site] = []
    findings: list[Finding] = []
    if path.relative_to(root) in (
        LOG_HEADER,
        LOG_SOURCE,
        CRASH_HEADER,
    ):
        return sites, findings
    for match in MACRO_CALL.finditer(mask):
        where = f"{relative}:{line_of(code, match.start())}"
        level = LEVEL_LETTERS[match.group(1)]
        literals = STRING_LITERALS.match(code, match.end())
        if not literals:
            findings.append(
                Finding("shape", where, "the format string must be a string literal")
            )
            continue
        fmt = "".join(
            unescape(m.group(1)) for m in ONE_LITERAL.finditer(literals.group(0))
        )
        event, keys, problems = parse_format(fmt)
        for problem in problems:
            findings.append(Finding("shape", where, problem))
        if event is not None:
            sites.append(Site(where, level, event, keys))
    for match in BYPASS.finditer(mask):
        where = f"{relative}:{line_of(code, match.start())}"
        name = match.group(0).rstrip("( \t\n")
        if name == "xvt_crash_note_writef" and path.relative_to(root) == CRASH_SOURCE:
            continue
        findings.append(
            Finding("bypass", where, f"{name} is called outside the log header")
        )
    return sites, findings


def load_catalog(root: Path) -> tuple[dict, list[Finding]]:
    """Return the catalog's events and the findings its own form raises.

    Raises OSError or ValueError when the file cannot be read or parsed.
    """
    where = CATALOG.as_posix()
    document = json.loads((root / CATALOG).read_text(encoding="utf-8"))
    events = document.get("events") if isinstance(document, dict) else None
    if not isinstance(events, dict):
        raise ValueError('the catalog needs an "events" object')
    findings: list[Finding] = []
    for event, entry in events.items():
        if not EVENT_ID.match(event):
            findings.append(
                Finding("catalog", where, f"{event!r} is not component.action")
            )
        if not isinstance(entry, dict):
            findings.append(
                Finding("catalog", where, f"{event}: entry is not an object")
            )
            continue
        level = entry.get("level")
        fields = entry.get("fields")
        text = entry.get("text")
        if level not in LEVEL_LETTERS.values():
            findings.append(
                Finding("catalog", where, f"{event}: level must be D, I, W, E or C")
            )
        if not isinstance(fields, list) or any(not isinstance(f, str) for f in fields):
            findings.append(
                Finding("catalog", where, f"{event}: fields must be a list of names")
            )
            fields = []
        elif len(set(fields)) != len(fields):
            findings.append(Finding("catalog", where, f"{event}: a field repeats"))
        if not isinstance(text, str) or not text:
            findings.append(
                Finding("catalog", where, f"{event}: text must be a sentence")
            )
            text = ""
        for name in TEMPLATE_FIELD.findall(text):
            if name not in fields:
                findings.append(
                    Finding(
                        "catalog", where, f"{event}: text names {{{name}}}, not a field"
                    )
                )
    return events, findings


def compare(sites: list[Site], events: dict) -> list[Finding]:
    """Return the findings where call sites and catalog entries disagree."""
    findings: list[Finding] = []
    written: dict[str, set[str]] = {}
    for site in sites:
        entry = events.get(site.event)
        if not isinstance(entry, dict):
            findings.append(
                Finding(
                    "uncataloged", site.where, f"{site.event} is not in the catalog"
                )
            )
            continue
        if entry.get("level") != site.level:
            findings.append(
                Finding(
                    "level",
                    site.where,
                    f"{site.event} is {site.level} here, {entry.get('level')} in the catalog",
                )
            )
        declared = entry.get("fields") if isinstance(entry.get("fields"), list) else []
        for key in site.keys:
            if key not in declared:
                findings.append(
                    Finding(
                        "fields", site.where, f"{site.event} writes {key}, not declared"
                    )
                )
        written.setdefault(site.event, set()).update(site.keys)
    for event, entry in events.items():
        if event not in written:
            findings.append(
                Finding("orphan", CATALOG.as_posix(), f"{event} has no call site")
            )
            continue
        declared = (
            entry.get("fields")
            if isinstance(entry, dict) and isinstance(entry.get("fields"), list)
            else []
        )
        for name in declared:
            if name not in written[event]:
                findings.append(
                    Finding(
                        "fields",
                        CATALOG.as_posix(),
                        f"{event} declares {name}, which no site writes",
                    )
                )
    return findings


def check(root: Path) -> tuple[list[Finding], int, int]:
    """Run every check under root; return findings, site count, entry count.

    Raises OSError or ValueError when the catalog cannot be read.
    """
    events, findings = load_catalog(root)
    sites: list[Site] = []
    for folder in SCANNED:
        for path in sorted((root / folder).rglob("*")):
            if path.suffix in (".c", ".h") and path.is_file():
                found, raised = scan_file(path, root)
                sites.extend(found)
                findings.extend(raised)
    findings.extend(compare(sites, events))
    return findings, len(sites), len(events)


def main(argv: list[str]) -> int:
    """Run the check from the command line and print the report."""
    if len(argv) > 1 or argv[:1] in (["-h"], ["--help"]):
        print(__doc__.strip())
        return 2
    root = Path(argv[0]).resolve() if argv else Path(__file__).resolve().parent.parent
    try:
        findings, sites, entries = check(root)
    except (OSError, ValueError) as err:
        print(
            f"log_catalog_check: cannot read {CATALOG.as_posix()}: {err}",
            file=sys.stderr,
        )
        return 2
    for finding in sorted(findings, key=lambda f: (f.kind, f.where, f.detail)):
        print(finding)
    verdict = "clean" if not findings else f"{len(findings)} findings"
    print(f"{sites} call sites, {entries} catalog entries, {verdict}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

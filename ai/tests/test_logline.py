"""Fence for logline and the event catalog.

The catalog fence reads every log call in the package and checks it against
``events.json``, as the engine's ``tools/log_catalog_check.py`` does for C:
each event is cataloged with its level and fields, each catalog entry is
used, and each line's shape is ``event key=value ...``.
"""

from __future__ import annotations

import ast
import datetime
import io
import json
import logging
import os
import pathlib
import re

import pytest

from jedimaster_ai import logline

PACKAGE = pathlib.Path(logline.__file__).parent
CATALOG = json.loads((PACKAGE / "events.json").read_text())
LETTER_OF_METHOD = {
    "debug": "D",
    "info": "I",
    "warning": "W",
    "error": "E",
    "critical": "C",
}
# A number is %d; a text value is "%s", quoted, as the engine's check asks.
WORD = re.compile(r'(?P<key>[a-z_]+)=(%d|"%s")')


def record(level: int, message: str, *args: object) -> logging.LogRecord:
    made = logging.LogRecord("jedimaster_ai.x", level, "x.py", 1, message, args, None)
    made.created = datetime.datetime(
        2026, 10, 12, 21, 3, 14, 641_900, tzinfo=datetime.UTC
    ).timestamp()
    return made


def test_a_line_is_time_letter_and_message() -> None:
    line = logline.LineFormatter().format(record(logging.INFO, "a.b n=%d", 7))
    assert line == "21:03:14.641 I a.b n=7"


@pytest.mark.parametrize(
    ("level", "letter"),
    [(logging.DEBUG, "D"), (logging.WARNING, "W"), (logging.ERROR, "E"), (50, "C")],
)
def test_each_level_has_its_letter(level: int, letter: str) -> None:
    assert logline.LineFormatter().format(record(level, "a.b")).split()[1] == letter


@pytest.mark.parametrize(
    ("path", "shown"),
    [("/srv/pilot/x", "~/x"), ("/srv/pilot", "~"), ("/srv/pilot2", "/srv/pilot2")],
)
def test_the_home_folder_is_written_as_a_tilde(path: str, shown: str) -> None:
    assert logline.shorten_home(path, "/srv/pilot") == shown


def test_the_systems_own_separator_follows_the_home_folder() -> None:
    home = os.path.join(os.sep + "srv", "pilot")
    assert logline.shorten_home(os.path.join(home, "x"), home) == "~" + os.sep + "x"


def test_the_header_states_the_runs_constants() -> None:
    lines = logline.header_lines(
        "0000abcd", datetime.date(2026, 10, 12), "/srv/pilot/g", "/srv/pilot"
    )
    assert lines == [
        "= jedimaster_ai run 0000abcd fmt 1 date 2026-10-12 tz utc",
        '= cwd "~/g"',
    ]


def test_install_writes_the_header_first_to_each_place(tmp_path) -> None:
    stream, log_file = io.StringIO(), tmp_path / "ai.log"
    log_file.write_text("an earlier run\n")
    logline.install(logging.INFO, "0000abcd", str(log_file), stream).info("a.b n=%d", 1)
    logging.getLogger("jedimaster_ai").handlers[1].close()
    shown = stream.getvalue().splitlines()
    assert shown[0].startswith("= jedimaster_ai run 0000abcd fmt 1 date ")
    assert shown[1].startswith('= cwd "')
    assert shown[2].endswith(" I a.b n=1")
    assert log_file.read_text().splitlines() == ["an earlier run", *shown]


def call_sites() -> list[tuple[str, ast.Call]]:
    """Return each logger call in the package, with where it is."""
    found = []
    for path in sorted(PACKAGE.rglob("*.py")):
        for node in ast.walk(ast.parse(path.read_text())):
            if (
                isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute)
                and isinstance(node.func.value, ast.Name)
                and node.func.value.id == "logger"
                and node.func.attr in LETTER_OF_METHOD
            ):
                found.append((f"{path.name}:{node.lineno}", node))
    return found


def test_every_call_site_is_cataloged_and_shaped() -> None:
    faults = []
    used = set()
    for where, call in call_sites():
        first = call.args[0]
        if not (isinstance(first, ast.Constant) and isinstance(first.value, str)):
            faults.append(f"{where}: the message is not one literal string")
            continue
        event, *words = first.value.split(" ")
        entry = CATALOG["events"].get(event)
        if entry is None:
            faults.append(f"{where}: {event} is not cataloged")
            continue
        used.add(event)
        if entry["level"] != LETTER_OF_METHOD[call.func.attr]:
            faults.append(f"{where}: {event} is level {entry['level']} in the catalog")
        matches = [WORD.fullmatch(word) for word in words]
        if not re.fullmatch(r"[a-z]+\.[a-z_]+", event) or not all(matches):
            faults.append(f"{where}: {first.value!r} is not event key=value ...")
            continue
        if [match["key"] for match in matches] != entry["fields"]:
            faults.append(f"{where}: {event}'s fields differ from the catalog's")
        if len(call.args) - 1 != len(words):
            faults.append(f"{where}: {event} passes {len(call.args) - 1} values")
    faults += [
        f"{event} is used by no call" for event in CATALOG["events"].keys() - used
    ]
    assert faults == []


def test_every_catalog_text_names_only_its_fields() -> None:
    for event, entry in CATALOG["events"].items():
        named = set(re.findall(r"\{(\w+)\}", entry["text"]))
        assert named <= set(entry["fields"]), event


def test_every_logger_is_named_for_its_module() -> None:
    for path in sorted(PACKAGE.rglob("*.py")):
        for node in ast.walk(ast.parse(path.read_text())):
            if (
                isinstance(node, ast.Call)
                and ast.unparse(node.func) == "logging.getLogger"
            ):
                argument = ast.unparse(node.args[0])
                assert argument == "__name__" or path.name == "logline.py", path.name

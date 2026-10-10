"""Build what the page tests need: a made-up install, the schema, messages.

Purpose:
    Give every ``test_page_*`` file the same synthetic install (menus of
    made-up missions, no game data), the same schema validator, and a way to
    write requests and check every message the launcher sends.

Flow:
    ``make_install`` writes menu files under a temporary folder;
    ``validator`` loads the published control schema; ``request`` builds a
    request's text; ``check_valid`` validates one message against the schema
    and returns it.

Invariants:
    - No game data: every title and file name is a generic word.

Call:
    ``install = make_install(tmp_path); check_valid(reply)``
"""

from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import Any

import jsonschema
from listdata import crlf

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "control.schema.json"
EXAMPLES = Path(__file__).resolve().parent / "control-examples"
LAUNCHER_VERSION = "0.1.0"


def make_install(root: Path) -> Path:
    """Write a made-up install under ``root`` and return its folder.

    The training menu has two sections and a locked entry; the melee menu
    has one entry; the combat menu holds a NUL byte, so the reader refuses
    it; the other menus are missing. Balance of Power's folder exists.
    """
    install = root / "XvT"
    files = {
        "Train/MISSION.LST": crlf(
            "[Basic]",
            "1",
            "ALPHA.TIE",
            "First Flight",
            "2",
            "& BRAVO.TIE",
            "Second Flight",
            "[Advanced]",
            "3",
            "CHARLIE.TIE",
            "Third Flight",
        ),
        "Melee/MISSION.LST": crlf("[Open]", "7", "DELTA.TIE", "Open Field"),
        "Combat/MISSION.LST": b"1\r\na.tie\x00\r\nbroken\r\n",
        "BalanceOfPower/TRAIN/notes.txt": b"",
    }
    for rel, data in files.items():
        path = install / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return install


def validator() -> jsonschema.Draft202012Validator:
    """Return a draft 2020-12 validator for the published control schema."""
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def check_valid(message: dict[str, Any]) -> dict[str, Any]:
    """Assert ``message`` satisfies the control schema; return it."""
    errors = sorted(validator().iter_errors(message), key=lambda e: list(e.path))
    assert errors == [], (message, [e.message for e in errors])
    return message


def request(ident: int, command: str, args: dict[str, Any] | None = None) -> str:
    """Return a request's JSON text."""
    return json.dumps({"id": ident, "command": command, "args": args or {}})

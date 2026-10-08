"""Write the mission, lists and icons JSON Schemas from the models.

Purpose:
    Keep the published JSON Schemas (``schema/mission.schema.json``,
    ``schema/lists.schema.json``, ``schema/icons.schema.json``) equal to what
    ``mission_schema()``, ``lists_schema()`` and ``icons_schema()`` build; a
    test fails when a file and its model drift.

Flow:
    Build each schema, dump it with sorted-free stable formatting, write it.

Invariants:
    - Output is deterministic: same model, same bytes.

Call:
    ``python tools/gen_schema.py``
"""

from __future__ import annotations

import json
import logging
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from jedimaster.icons.to_json import icons_schema  # noqa: E402
from jedimaster.lists.to_json import lists_schema  # noqa: E402
from jedimaster.mission.to_json import mission_schema  # noqa: E402

logger = logging.getLogger("gen_schema")

SCHEMA_PATH = HERE.parents[1] / "schema" / "mission.schema.json"
LISTS_SCHEMA_PATH = HERE.parents[1] / "schema" / "lists.schema.json"
ICONS_SCHEMA_PATH = HERE.parents[1] / "schema" / "icons.schema.json"


def schema_text() -> str:
    """Return the mission schema file's text: indented JSON and a newline."""
    return json.dumps(mission_schema(), indent=2) + "\n"


def lists_schema_text() -> str:
    """Return the lists schema file's text: indented JSON and a newline."""
    return json.dumps(lists_schema(), indent=2) + "\n"


def icons_schema_text() -> str:
    """Return the icons schema file's text: indented JSON and a newline."""
    return json.dumps(icons_schema(), indent=2) + "\n"


def main() -> int:
    """Write the three schema files; return 0."""
    for path, text in (
        (SCHEMA_PATH, schema_text()),
        (LISTS_SCHEMA_PATH, lists_schema_text()),
        (ICONS_SCHEMA_PATH, icons_schema_text()),
    ):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Write schema/mission.schema.json from the model.

Purpose:
    Keep the published JSON Schema equal to what ``mission_schema()`` builds
    from the dataclasses; a test fails when the file and the model drift.

Flow:
    Build the schema, dump it with sorted-free stable formatting, write it.

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

from jedimaster.mission.to_json import mission_schema  # noqa: E402

logger = logging.getLogger("gen_schema")

SCHEMA_PATH = HERE.parents[1] / "schema" / "mission.schema.json"


def schema_text() -> str:
    """Return the schema file's text: indented JSON plus a final newline."""
    return json.dumps(mission_schema(), indent=2) + "\n"


def main() -> int:
    """Write the schema file; return 0."""
    SCHEMA_PATH.parent.mkdir(parents=True, exist_ok=True)
    SCHEMA_PATH.write_text(schema_text(), encoding="utf-8")
    print(f"wrote {SCHEMA_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

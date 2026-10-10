"""Write the package's JSON Schemas from the models.

Purpose:
    Keep the published JSON Schemas (``schema/mission.schema.json``,
    ``schema/lists.schema.json``, ``schema/icons.schema.json``,
    ``schema/text.schema.json``, ``schema/fonts.schema.json``,
    ``schema/pictures.schema.json``, ``schema/pilot.schema.json``,
    ``schema/models.schema.json``, ``schema/movies.schema.json``,
    ``schema/control.schema.json``, ``schema/briefing.schema.json``) equal to what ``mission_schema()``,
    ``lists_schema()``, ``icons_schema()``, ``text_schema()``,
    ``fonts_schema()``, ``pictures_schema()``, ``pilot_schema()``,
    ``models_schema()``, ``movies_schema()`` and ``control_schema()`` and ``briefing_schema()`` build; a test fails when a file and its model drift.

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

from jedimaster.briefing.schema import briefing_schema  # noqa: E402
from jedimaster.fonts.to_json import fonts_schema  # noqa: E402
from jedimaster.icons.to_json import icons_schema  # noqa: E402
from jedimaster.lists.to_json import lists_schema  # noqa: E402
from jedimaster.mission.to_json import mission_schema  # noqa: E402
from jedimaster.models.to_json import models_schema  # noqa: E402
from jedimaster.movies.to_json import movies_schema  # noqa: E402
from jedimaster.page.schema import control_schema  # noqa: E402
from jedimaster.pictures.to_json import pictures_schema  # noqa: E402
from jedimaster.pilot.to_json import pilot_schema  # noqa: E402
from jedimaster.text.to_json import text_schema  # noqa: E402

logger = logging.getLogger("gen_schema")

SCHEMA_PATH = HERE.parents[1] / "schema" / "mission.schema.json"
LISTS_SCHEMA_PATH = HERE.parents[1] / "schema" / "lists.schema.json"
ICONS_SCHEMA_PATH = HERE.parents[1] / "schema" / "icons.schema.json"
TEXT_SCHEMA_PATH = HERE.parents[1] / "schema" / "text.schema.json"
FONTS_SCHEMA_PATH = HERE.parents[1] / "schema" / "fonts.schema.json"
PICTURES_SCHEMA_PATH = HERE.parents[1] / "schema" / "pictures.schema.json"
PILOT_SCHEMA_PATH = HERE.parents[1] / "schema" / "pilot.schema.json"
MODELS_SCHEMA_PATH = HERE.parents[1] / "schema" / "models.schema.json"
MOVIES_SCHEMA_PATH = HERE.parents[1] / "schema" / "movies.schema.json"
CONTROL_SCHEMA_PATH = HERE.parents[1] / "schema" / "control.schema.json"
BRIEFING_SCHEMA_PATH = HERE.parents[1] / "schema" / "briefing.schema.json"


def schema_text() -> str:
    """Return the mission schema file's text: indented JSON and a newline."""
    return json.dumps(mission_schema(), indent=2) + "\n"


def lists_schema_text() -> str:
    """Return the lists schema file's text: indented JSON and a newline."""
    return json.dumps(lists_schema(), indent=2) + "\n"


def icons_schema_text() -> str:
    """Return the icons schema file's text: indented JSON and a newline."""
    return json.dumps(icons_schema(), indent=2) + "\n"


def text_schema_text() -> str:
    """Return the text schema file's text: indented JSON and a newline."""
    return json.dumps(text_schema(), indent=2) + "\n"


def fonts_schema_text() -> str:
    """Return the fonts schema file's text: indented JSON and a newline."""
    return json.dumps(fonts_schema(), indent=2) + "\n"


def pictures_schema_text() -> str:
    """Return the pictures schema file's text: indented JSON and a newline."""
    return json.dumps(pictures_schema(), indent=2) + "\n"


def pilot_schema_text() -> str:
    """Return the pilot schema file's text: indented JSON and a newline."""
    return json.dumps(pilot_schema(), indent=2) + "\n"


def models_schema_text() -> str:
    """Return the models schema file's text: indented JSON and a newline."""
    return json.dumps(models_schema(), indent=2) + "\n"


def movies_schema_text() -> str:
    """Return the movies schema file's text: indented JSON and a newline."""
    return json.dumps(movies_schema(), indent=2) + "\n"


def control_schema_text() -> str:
    """Return the control schema file's text: indented JSON and a newline."""
    return json.dumps(control_schema(), indent=2) + "\n"


def briefing_schema_text() -> str:
    """Return the briefing schema file's text: indented JSON and a newline."""
    return json.dumps(briefing_schema(), indent=2) + "\n"


def main() -> int:
    """Write the eleven schema files; return 0."""
    for path, text in (
        (SCHEMA_PATH, schema_text()),
        (LISTS_SCHEMA_PATH, lists_schema_text()),
        (ICONS_SCHEMA_PATH, icons_schema_text()),
        (TEXT_SCHEMA_PATH, text_schema_text()),
        (FONTS_SCHEMA_PATH, fonts_schema_text()),
        (PICTURES_SCHEMA_PATH, pictures_schema_text()),
        (PILOT_SCHEMA_PATH, pilot_schema_text()),
        (MODELS_SCHEMA_PATH, models_schema_text()),
        (MOVIES_SCHEMA_PATH, movies_schema_text()),
        (CONTROL_SCHEMA_PATH, control_schema_text()),
        (BRIEFING_SCHEMA_PATH, briefing_schema_text()),
    ):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

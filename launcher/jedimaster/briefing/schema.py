"""The JSON Schema of the briefing bundle, built from its dataclasses.

Purpose:
    Publish the JSON Schema (draft 2020-12) that every bundle satisfies,
    from the same classes the control list embeds.

Flow:
    ``briefing_schema`` hands ``BriefingBundle`` to the page package's
    schema walker.

Invariants:
    - Same model, same dict.
    - This module is not imported by ``jedimaster.briefing`` itself: the
      page package imports the model, and the walker lives in the page
      package.

Call:
    ``json.dumps(briefing_schema(), indent=2)``
"""

from __future__ import annotations

import logging
from typing import Any

from ..page.schema import schema_of
from .model import BriefingBundle

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/briefing.schema.json"


def briefing_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of one briefing bundle.

    Always returns the same dict: a closed object with every field
    required. Does not encode that a team's briefing index names a briefing
    in the list, or that a craft type has a box.
    """
    return schema_of(BriefingBundle, SCHEMA_ID, "Briefing bundle")

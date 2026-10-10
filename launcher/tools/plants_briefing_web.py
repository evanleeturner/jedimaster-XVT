"""All the planted faults of the briefing player, joined.

Purpose:
    Join the four tables so the page runner takes one.

Flow:
    Each table lives in its own module to keep each file small.

Invariants:
    - Ids are unique across the tables, all starting ``web-``.

Call:
    ``from plants_briefing_web import BRIEFING_WEB_PLANTS``
"""

from __future__ import annotations

import logging

from plants_briefing_core_web import BRIEFING_CORE_PLANTS
from plants_briefing_output_web import BRIEFING_OUTPUT_PLANTS
from plants_briefing_page_web import BRIEFING_PAGE_PLANTS
from plants_briefing_text_web import BRIEFING_TEXT_PLANTS
from plants_common import Plant

logger = logging.getLogger(__name__)

BRIEFING_WEB_PLANTS: list[Plant] = [
    *BRIEFING_CORE_PLANTS,
    *BRIEFING_TEXT_PLANTS,
    *BRIEFING_OUTPUT_PLANTS,
    *BRIEFING_PAGE_PLANTS,
]

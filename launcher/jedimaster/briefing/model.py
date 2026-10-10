"""The briefing bundle as dataclasses: what the player needs for one mission.

Purpose:
    Name, once, every field of the bundle the briefing player is given for
    one mission. The control list (``jedimaster.page.protocol``) and the
    bundle's own schema (``schema``) are both built from these classes.

Flow:
    ``build.build_bundle`` fills a ``BriefingBundle`` as a plain dict of
    these shapes; ``schema.briefing_schema`` and the control schema walk the
    classes through their type hints.

Invariants:
    - The classes hold no logic and import nothing from ``page``.
    - Lists with a fixed length (``TEAM_COUNT`` teams, ``TEXT_COUNT`` texts,
      ``POINT_COUNT`` points, ``BOX_COUNT`` boxes, ``CRAFT_COUNT`` crafts,
      ``IFF_COUNT`` IFFs, ``GLYPH_COUNT`` widths) are not pinned in the schema:
      a tuple type per list would bloat the generated page types.
    - Text is kept as the game stores it: one character per byte
      (ISO-8859-1), so a text character is a code from 0 to 255.

Call:
    ``from jedimaster.briefing.model import BriefingBundle``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from dataclasses import field
from typing import Literal

logger = logging.getLogger(__name__)

BUNDLE_FORMAT = "jedimaster.briefing"
BUNDLE_FORMAT_VERSION = 1
TEAM_COUNT = 10
BRIEFING_COUNT = 8
TEXT_COUNT = 32
POINT_COUNT = 8
BOX_COUNT = 70
CRAFT_COUNT = 106
IFF_COUNT = 256
GLYPH_COUNT = 256


@dataclass
class EventData:
    """One script event: its time, its type and its variables."""

    time: int
    type: int
    variables: list[int]


@dataclass
class BriefingData:
    """One briefing of the mission: running time, events, label and caption texts."""

    index: int = field(metadata={"minimum": 0, "maximum": BRIEFING_COUNT - 1})
    running_time: int
    events: list[EventData]
    labels: list[str]
    captions: list[str]


@dataclass
class PointData:
    """One briefing point of a flight group: map x and y, and whether it is on."""

    x: int
    y: int
    enabled: bool


@dataclass
class GroupData:
    """One flight group: what the map draws for it."""

    number: int = field(metadata={"minimum": 0})
    name: str
    craft_type: int
    iff: int
    team: int
    player_number: int
    points: list[PointData]


@dataclass
class TeamData:
    """One team: its name and the briefing it sees (None: the default one)."""

    team: int = field(metadata={"minimum": 0, "maximum": TEAM_COUNT - 1})
    name: str
    briefing: int | None


@dataclass
class BoxData:
    """One icon box in a sheet, all four edges inclusive."""

    left: int
    top: int
    right: int
    bottom: int


@dataclass
class FontData:
    """Font 10: spacing, height, glyph widths, text colors and the atlas cells."""

    spacing: int
    height: int
    widths: list[int]
    text_colors: list[int]
    columns: int
    cell_width: int
    cell_height: int
    picture: str


@dataclass
class SoundData:
    """One sound the player uses: its name in the game's list and its art name."""

    name: str
    picture: str


@dataclass
class IconSheetData:
    """One icon sheet: its name and its art name."""

    name: str
    picture: str


@dataclass
class BriefingBundle:
    """Everything the briefing player needs for one mission file."""

    format: Literal["jedimaster.briefing"]
    format_version: Literal[1]
    front_string: str
    teams: list[TeamData]
    briefings: list[BriefingData]
    groups: list[GroupData]
    boxes: list[BoxData]
    craft_boxes: list[int]
    iff_sheets: list[str]
    sheets: list[IconSheetData]
    grey_sheet: IconSheetData | None
    font: FontData
    sounds: list[SoundData]

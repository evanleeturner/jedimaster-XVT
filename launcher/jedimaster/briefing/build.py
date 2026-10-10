"""Build the briefing bundle of one mission from an install.

Purpose:
    Gather, from a mission file and the install's own files, everything
    the briefing player needs: which briefing each team sees, each briefing
    used, the flight groups with their briefing points, the team names,
    the page word, the icon tables and font 10's metrics.

Flow:
    ``team_briefing`` applies the rule "the last briefing whose flag for
    the team is set". ``build_bundle`` reads the mission with
    ``jedimaster.mission``, the page word with ``jedimaster.text``, the
    font with ``jedimaster.fonts`` and the sheets and sounds with the icon
    and list readers, and returns the bundle as plain JSON-ready data.

Invariants:
    - Only the briefings some team sees are in the bundle, in index order.
    - A team with no briefing has ``briefing`` None; the default briefing
      (200 frames, no events but the end, no texts) belongs to the player.
    - Texts keep one character per byte (ISO-8859-1).
    - Art that is missing leaves out its entry (a sheet, a sound); a
      missing font is an error, since nothing can be drawn without it.

Call:
    ``data = build_bundle(install, read_mission(path))``
"""

from __future__ import annotations

import logging
from dataclasses import asdict
from pathlib import Path
from typing import Any

from ..fonts import TEXT_COLORS
from ..fonts.to_json import atlas_name
from ..fonts.to_json import font_atlas
from ..icons import BOXES
from ..icons import CRAFT_BOXES
from ..icons import icons_view
from ..icons import picture_name
from ..icons import sheet_for_iff
from ..lists.files import resolve
from ..mission.model import Briefing
from ..mission.model import Mission
from ..text.menus import front_text
from ..text.menus import read_front
from .art import read_font10
from .art import sound_art_name
from .art import sound_files
from .model import BoxData
from .model import BriefingBundle
from .model import BriefingData
from .model import EventData
from .model import FontData
from .model import GroupData
from .model import IconSheetData
from .model import IFF_COUNT
from .model import PointData
from .model import SoundData
from .model import TEAM_COUNT
from .model import TeamData
from .names import END_EVENT
from .names import EVENT_VARIABLES
from .names import FONT_GAME_NAME
from .names import FRONT_FILE
from .names import GREY_SHEET
from .names import ICON_SHEETS
from .names import NO_TEXT
from .names import PAGE_STRING
from .names import SOUND_NAMES

logger = logging.getLogger(__name__)

FIRST_BRIEFING_POINT = 14
EVENT_AREA = 400
"""SHORTs in a briefing's event area; the reader trims trailing zeros."""
POINTS = 8


class BriefingBuildError(ValueError):
    """The bundle cannot be built from this install."""


def team_briefing(viewed_by: list[list[int]], team: int) -> int | None:
    """Return the index of the briefing ``team`` sees, or None for the default.

    ``viewed_by[i]`` is briefing ``i``'s ten team flags. The last briefing
    whose flag for the team is not 0 wins; None when no flag is set. Does
    not check the list lengths beyond reading ``team`` from each.
    """
    seen = None
    for index, flags in enumerate(viewed_by):
        if flags[team]:
            seen = index
    return seen


def _page_word(install: Path) -> str:
    path = resolve(install, FRONT_FILE)
    if path is None or not path.is_file():
        logger.warning("%s not found: the page word is %r", FRONT_FILE, NO_TEXT)
        return NO_TEXT
    entries = read_front(path)
    return front_text(entries, PAGE_STRING).decode("iso-8859-1")


def _groups(mission: Mission) -> list[GroupData]:
    groups = []
    for number, group in enumerate(mission.flight_groups):
        points = [
            PointData(p.x, p.y, bool(p.enabled))
            for p in group.waypoints[FIRST_BRIEFING_POINT:][:POINTS]
        ]
        groups.append(
            GroupData(
                number,
                group.name,
                group.craft_type,
                group.iff,
                group.team,
                group.player_number,
                points,
            )
        )
    return groups


def walk_events(briefing: Briefing) -> list[EventData]:
    """Return the briefing's events read with the setup screen's own counts.

    The mission reader stops at an event type its format description does
    not list; this walk joins the events it read and the raw shorts it kept
    after them (zeros put back up to the area's 400) and reads them again with
    ``EVENT_VARIABLES``. It stops after
    the end event, at a type outside 0 to 34, or when an event runs past the
    shorts there are. Does not check event times.
    """
    shorts = [
        s for e in briefing.events for s in (e.time, e.type, *e.variables)
    ] + list(briefing.events_tail)
    shorts += [0] * (EVENT_AREA - len(shorts))
    events: list[EventData] = []
    at = 0
    while at + 2 <= len(shorts):
        time, kind = shorts[at], shorts[at + 1]
        count = EVENT_VARIABLES.get(kind)
        if count is None or at + 2 + count > len(shorts):
            logger.debug("event walk stops at short %d (type %d)", at, kind)
            break
        events.append(EventData(time, kind, shorts[at + 2 : at + 2 + count]))
        at += 2 + count
        if kind == END_EVENT:
            break
    return events


def _briefings(mission: Mission, used: list[int]) -> list[BriefingData]:
    return [
        BriefingData(
            index,
            mission.briefings[index].running_time,
            walk_events(mission.briefings[index]),
            list(mission.briefings[index].tags),
            list(mission.briefings[index].strings),
        )
        for index in used
    ]


def _font(install: Path) -> FontData:
    font = read_font10(install)
    if font is None:
        raise BriefingBuildError(f"{FONT_GAME_NAME} cannot be read from the install")
    atlas = font_atlas(font)
    return FontData(
        font.spacing,
        font.height,
        list(font.widths),
        [TEXT_COLORS[byte].color for byte in sorted(TEXT_COLORS)],
        atlas.width // atlas.cell_width,
        atlas.cell_width,
        atlas.cell_height,
        atlas_name(FONT_GAME_NAME),
    )


def _sheets(install: Path) -> tuple[list[IconSheetData], IconSheetData | None]:
    try:
        have = icons_view(install).sheets
    except (OSError, ValueError) as exc:
        logger.warning("no icon sheets: %s", exc)
        have = {}
    sheets = [IconSheetData(n, picture_name(n)) for n in ICON_SHEETS if n in have]
    grey = IconSheetData(GREY_SHEET, picture_name(GREY_SHEET))
    return sheets, grey if GREY_SHEET in have else None


def build_bundle(install: Path, mission: Mission) -> dict[str, Any]:
    """Return the bundle for ``mission`` as JSON-ready data.

    Always returns a dict that satisfies ``schema/briefing.schema.json``.
    Raises ``BriefingBuildError`` when font 10 cannot be read, and
    ``OSError`` or a list or font reader's error when a file the bundle
    needs is unreadable. A missing front text, icon sheet or sound is left
    out with a WARNING. Does not check the mission's references (a craft
    type without an icon, a group number past the list).
    """
    viewed_by = [list(b.viewed_by_team) for b in mission.briefings]
    choices = [team_briefing(viewed_by, team) for team in range(TEAM_COUNT)]
    used = sorted({c for c in choices if c is not None})
    teams = [
        TeamData(team, mission.teams[team].name, choice)
        for team, choice in enumerate(choices)
    ]
    sheets, grey = _sheets(install)
    found = sound_files(install)
    sounds = [SoundData(n, sound_art_name(n)) for n in SOUND_NAMES if n in found]
    bundle = BriefingBundle(
        "jedimaster.briefing",
        1,
        _page_word(install),
        teams,
        _briefings(mission, used),
        _groups(mission),
        [BoxData(*box) for box in BOXES],
        list(CRAFT_BOXES),
        [sheet_for_iff(iff) for iff in range(IFF_COUNT)],
        sheets,
        grey,
        _font(install),
        sounds,
    )
    logger.info(
        "bundle: %d briefings used, %d groups, %d sheets, %d sounds",
        len(used),
        len(bundle.groups),
        len(sheets),
        len(sounds),
    )
    return asdict(bundle)

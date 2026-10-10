"""The control list: every message the page and the launcher exchange.

Purpose:
    Name, as dataclasses, the one fixed list of commands (revision 3), their
    arguments and results, the three pushes and the error codes. The schema
    (``schema``) is built from these classes, so the list has one source.

Flow:
    A request is one of the ``*Request`` classes; the launcher answers with
    ``OkReply`` (its ``result`` one of the ``*Result`` classes) or
    ``ErrorReply``; it also sends ``StatusPush`` and ``SettingsChangedPush`` and ``ShowMissionPush``.
    The classes describe the JSON shapes; ``control`` builds the dicts.

Invariants:
    - Names and values of settings come only from ``SETTINGS`` and
      ``ART_SCALING_VALUES``.
    - An id is an integer from 1 to ``MAX_ID`` in a request; a reply uses
      ``UNKNOWN_ID`` (0) only when the request's id could not be read.
    - The classes hold no logic.

Call:
    ``from jedimaster.page.protocol import COMMANDS, MAX_MESSAGE_BYTES``
"""

from __future__ import annotations

import logging
import typing
from dataclasses import dataclass
from dataclasses import field
from typing import Literal

from ..briefing.model import BriefingBundle

logger = logging.getLogger(__name__)

SCHEMA_REVISION = 3
"""The revision of the control list; a change to any message raises it."""

MAX_MESSAGE_BYTES = 64 * 1024
MAX_ID = 2**31 - 1
UNKNOWN_ID = 0

ArtScaling = Literal["whole_pixels", "engine_fit", "sharp_bilinear"]
ART_SCALING_VALUES: tuple[str, ...] = typing.get_args(ArtScaling)
SETTINGS: dict[str, tuple[str, ...]] = {"art_scaling": ART_SCALING_VALUES}
"""Each setting's name and the values it may take."""
DEFAULT_SETTINGS: dict[str, str] = {"art_scaling": "whole_pixels"}

MissionTypeName = Literal[
    "training", "melee", "tournament", "combat", "battle", "campaign"
]
MISSION_TYPE_NAMES: tuple[str, ...] = typing.get_args(MissionTypeName)
"""The six mission types a page can be told to show."""

ERROR_CODES = ("bad_message", "unknown_command", "bad_arguments")
ErrorCode = Literal["bad_message", "unknown_command", "bad_arguments"]


ID_RANGE = {"minimum": 1, "maximum": MAX_ID}
REPLY_ID_RANGE = {"minimum": 0, "maximum": MAX_ID}


# ---- arguments and results ------------------------------------------------


@dataclass
class NoArgs:
    """No arguments."""


@dataclass
class HelloArgs:
    """The page announces its own version."""

    page_version: str = field(metadata={"minLength": 1, "maxLength": 64})


@dataclass
class ArtScalingChange:
    """Set the art scaling to one of its values."""

    name: Literal["art_scaling"]
    value: ArtScaling


@dataclass
class Settings:
    """The launcher's settings."""

    art_scaling: ArtScaling


@dataclass
class ShowMissionArgs:
    """The mission to show: its type and its id in the network menu of that type."""

    mission_type: MissionTypeName
    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})


@dataclass
class BriefingGetArgs:
    """The mission to brief: its type and its id in the network menu of that type."""

    mission_type: MissionTypeName
    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})


@dataclass
class HelloResult:
    """The launcher's version and the revision of the control list."""

    launcher_version: str
    schema_revision: Literal[3]


@dataclass
class InstallStatusResult:
    """The install found (home shown as ~) and whether Balance of Power is in it."""

    found: bool
    path: str | None
    balance_of_power: bool


@dataclass
class MenuEntryData:
    """One mission the game's network menu lists."""

    section: str
    id: int
    available: bool
    file: str
    title: str


@dataclass
class MenuData:
    """One mission type's network menu, or the fact that it does not resolve."""

    mission_type: str
    game_path: str
    resolved: bool
    entries: list[MenuEntryData]


@dataclass
class MissionsListResult:
    """The network menu of every mission type."""

    menus: list[MenuData]


@dataclass
class SettingsResult:
    """The settings as they stand."""

    settings: Settings


@dataclass
class ShowMissionResult:
    """Whether the mission was found in its menu and so shown."""

    shown: bool


@dataclass
class BriefingGetResult:
    """Whether the mission was found and read, and its briefing bundle."""

    found: bool
    bundle: BriefingBundle | None


# ---- requests -------------------------------------------------------------


@dataclass
class HelloRequest:
    """Ask the launcher to introduce itself."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["hello"]
    args: HelloArgs


@dataclass
class InstallStatusRequest:
    """Ask which install the launcher found."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["install.status"]
    args: NoArgs


@dataclass
class MissionsListRequest:
    """Ask for the network menu of every mission type."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["missions.list"]
    args: NoArgs


@dataclass
class SettingsGetRequest:
    """Ask for the settings."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["settings.get"]
    args: NoArgs


@dataclass
class SettingsSetRequest:
    """Change one setting."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["settings.set"]
    args: ArtScalingChange


@dataclass
class ShowMissionRequest:
    """Tell every open page to show one mission of the network menus."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["page.show_mission"]
    args: ShowMissionArgs


@dataclass
class BriefingGetRequest:
    """Ask for the briefing bundle of one mission of the network menus."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["briefing.get"]
    args: BriefingGetArgs


REQUEST_CLASSES = (
    HelloRequest,
    InstallStatusRequest,
    MissionsListRequest,
    SettingsGetRequest,
    SettingsSetRequest,
    ShowMissionRequest,
    BriefingGetRequest,
)
COMMANDS: tuple[str, ...] = tuple(
    typing.get_args(typing.get_type_hints(cls)["command"])[0] for cls in REQUEST_CLASSES
)
"""The names of the fixed list, in the table's order."""

# ---- replies and pushes ---------------------------------------------------


@dataclass
class OkReply:
    """The command ran; its result."""

    id: int = field(metadata=REPLY_ID_RANGE)
    ok: Literal[True]
    result: (
        HelloResult
        | InstallStatusResult
        | MissionsListResult
        | SettingsResult
        | ShowMissionResult
        | BriefingGetResult
    )


@dataclass
class ErrorBody:
    """Why the command did not run."""

    code: ErrorCode
    message: str


@dataclass
class ErrorReply:
    """The command did not run; the reason."""

    id: int = field(metadata=REPLY_ID_RANGE)
    ok: Literal[False]
    error: ErrorBody


@dataclass
class StatusData:
    """What the launcher tells a page that just connected."""

    launcher_version: str
    install_found: bool


@dataclass
class StatusPush:
    """Sent to a page when it connects."""

    event: Literal["status"]
    data: StatusData


@dataclass
class SettingsChangedPush:
    """Sent to every open page after a setting changed."""

    event: Literal["settings.changed"]
    data: SettingsResult


@dataclass
class ShownMission:
    """The mission a page is told to show."""

    mission_type: MissionTypeName
    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})
    title: str


@dataclass
class ShowMissionPush:
    """Sent to every open page when a mission is to be shown."""

    event: Literal["page.show_mission"]
    data: ShownMission


MESSAGE_CLASSES = (
    *REQUEST_CLASSES,
    OkReply,
    ErrorReply,
    StatusPush,
    SettingsChangedPush,
    ShowMissionPush,
)

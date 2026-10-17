"""The control list: every message the page and the launcher exchange.

Purpose:
    Name, as dataclasses, the one fixed list of commands (revision 9), their
    arguments and results, the pushes and the error codes. The schema
    (``schema``) is built from these classes, so the list has one source.

Flow:
    A request is one of the ``*Request`` classes; the launcher answers with
    ``OkReply`` (its ``result`` one of the ``*Result`` classes) or
    ``ErrorReply``; it also sends ``StatusPush``, ``SettingsChangedPush``,
    ``ShowMissionPush``, ``LaunchStartedPush``, ``LaunchEndedPush`` (with the
    flown battle's ``Debrief``), ``OptionsChangedPush`` and ``PilotChangedPush``.
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

SCHEMA_REVISION = 10
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

SideName = Literal["rebel", "imperial", "none"]
SIDE_NAMES: tuple[str, ...] = typing.get_args(SideName)
PilotSideName = Literal["rebel", "imperial"]
"""The two sides a solo list is kept for; a solo mission is shown by one of them."""
"""The three sides a solo battle names: ``none`` for a type that takes no side."""

OptionName = Literal[
    "difficulty",
    "collisions",
    "craft_jumping",
    "random_setup",
    "battle_length",
    "mission_time_limit",
    "continue_sequence",
    "sith",
]
OPTION_NAMES: tuple[str, ...] = typing.get_args(OptionName)
"""The eight game options, in the order the menus list them."""
DifficultyText = Literal["easy", "medium", "hard", "easy_cheat"]
SwitchText = Literal["on", "off"]
BattleLengthText = Literal["2", "3", "4"]
TimeLimitText = Literal[
    "none", "default", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20",
]  # fmt: skip
SequenceText = Literal["restart", "continue"]
SithText = Literal["none", "apprentices", "lord"]
LaunchStatus = Literal[
    "flown", "refused", "not_reached", "failed", "cancelled", "error"
]
LAUNCH_STATUSES: tuple[str, ...] = typing.get_args(LaunchStatus)
"""How a launch can end: the engine's four results, ``cancelled`` and ``error``."""
MAX_PLAYER_DIGITS = 20
"""A player is named by a Discord account id: 1 to 20 digits, as text."""

OutcomeName = Literal["none", "completed", "not_completed"]
"""How a flown battle went for the pilot's team, as the result says."""

ERROR_CODES = ("bad_message", "unknown_command", "bad_arguments")
ErrorCode = Literal["bad_message", "unknown_command", "bad_arguments"]


ID_RANGE = {"minimum": 1, "maximum": MAX_ID}
COUNT = {"minimum": 0}
ENTRY = {"minimum": 0, "maximum": 7}
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
class ShowSoloMissionArgs:
    """The mission to show: an entry of a sided kind's solo list, by side and file."""

    mission_type: MissionTypeName
    side: PilotSideName
    file: str


@dataclass
class BriefingGetArgs:
    """The mission to brief: its type and its id in the network menu of that type."""

    mission_type: MissionTypeName
    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})


@dataclass
class BriefingGetSoloArgs:
    """The mission to brief: an entry of a sided kind's solo list, by side and file."""

    mission_type: MissionTypeName
    side: PilotSideName
    file: str


@dataclass
class SoloMenuArgs:
    """A kind of battle and a side: the solo list to read."""

    mission_type: MissionTypeName
    side: SideName


@dataclass
class LaunchSoloArgs:
    """One battle to fly: who asks, the kind, the side, the mission's file and the pilot."""

    player: str = field(metadata={"minLength": 1, "maxLength": MAX_PLAYER_DIGITS})
    mission_type: MissionTypeName
    side: SideName
    mission: str = field(metadata={"minLength": 1, "maxLength": 64})
    pilot: str = field(metadata={"minLength": 1, "maxLength": 12})


@dataclass
class LaunchCancelArgs:
    """The launch to stop."""

    launch_id: int = field(metadata={"minimum": 1, "maximum": MAX_ID})


@dataclass
class OptionsGetArgs:
    """The player whose game options to read; null is the account this launcher answers."""

    player: str | None


@dataclass
class OptionsSetArgs:
    """Set one game option of a player to one of its choices."""

    player: str = field(metadata={"minLength": 1, "maxLength": MAX_PLAYER_DIGITS})
    name: OptionName
    value: str


@dataclass
class HelloResult:
    """The launcher's version and the revision of the control list."""

    launcher_version: str
    schema_revision: Literal[10]


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
class SoloEntryData:
    """One mission a solo list offers; every one flies, opened by the pilot or not."""

    section: str
    file: str
    title: str


@dataclass
class SoloMenuResult:
    """A solo list, or the fact that it does not resolve."""

    mission_type: MissionTypeName
    side: SideName
    game_path: str
    resolved: bool
    entries: list[SoloEntryData]


@dataclass
class PilotsListResult:
    """The pilots the game has, and whether a battle can be flown at all.

    ``found`` is false when no pilot folder is known or it cannot be read;
    ``engine_given`` is false when the launcher was started without the game
    program, so no battle can be flown.
    """

    found: bool
    pilots: list[str]
    engine_given: bool


@dataclass
class LaunchSoloResult:
    """Whether the battle started (``launch_id`` is 0 when not), and one plain sentence."""

    started: bool
    launch_id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})
    message: str


@dataclass
class LaunchCancelResult:
    """Whether a flying launch was stopped, and one plain sentence."""

    cancelled: bool
    message: str


@dataclass
class GameOptions:
    """The game options a player has: what they set, else the option's default."""

    difficulty: DifficultyText
    collisions: SwitchText
    craft_jumping: SwitchText
    random_setup: SwitchText
    battle_length: BattleLengthText
    mission_time_limit: TimeLimitText
    continue_sequence: SequenceText
    sith: SithText


@dataclass
class OptionsResult:
    """One player's game options (``player`` is null when no account is linked)."""

    player: str | None
    options: GameOptions


@dataclass
class BriefingGetResult:
    """Whether the mission was found and read, and its briefing bundle."""

    found: bool
    bundle: BriefingBundle | None


@dataclass
class ResultsGetArgs:
    """The kept debriefing to read."""

    result_id: int = field(metadata=ID_RANGE)


@dataclass
class DebriefPlayer:
    """One player as the battle's end counted it, with its rank and flight group named."""

    entry: int = field(metadata=ENTRY)
    name: str = field(metadata={"maxLength": 12})
    rating: int
    rank: str
    team: int = field(metadata={"minimum": -1})
    flight_group: int = field(metadata=COUNT)
    flight_group_name: str
    score: int
    kills: int = field(metadata=COUNT)
    shared: int = field(metadata=COUNT)
    assists: int = field(metadata=COUNT)
    losses: int = field(metadata=COUNT)
    shots: int
    hits: int
    warheads: int = field(metadata=COUNT)
    warhead_hits: int = field(metadata=COUNT)
    has_left: bool


@dataclass
class DebriefTeam:
    """One team of the battle, with its name from the mission."""

    team: int = field(metadata=ENTRY)
    name: str
    completed: bool
    score: int = field(metadata=COUNT)
    kills: int = field(metadata=COUNT)
    shared: int = field(metadata=COUNT)
    losses: int = field(metadata=COUNT)
    seconds: int


@dataclass
class CraftKills:
    """The pilot's kills of one craft type, named as the statistics page names it."""

    craft_type: int = field(metadata={"minimum": 0, "maximum": 99})
    name: str
    kills: int = field(metadata=COUNT)
    shared: int = field(metadata=COUNT)
    assists: int = field(metadata=COUNT)


@dataclass
class RankKills:
    """The pilot's kills of pilots of one rank."""

    rank: int = field(metadata={"minimum": 0, "maximum": 24})
    kills: int = field(metadata=COUNT)
    shared: int = field(metadata=COUNT)
    assists: int = field(metadata=COUNT)


@dataclass
class RankTimes:
    """How often a killer of one rank destroyed the pilot."""

    rank: int = field(metadata={"minimum": 0, "maximum": 24})
    times: int = field(metadata=COUNT)


@dataclass
class PlayerKills:
    """Kills between the pilot and one player."""

    entry: int = field(metadata=ENTRY)
    name: str
    kills: int = field(metadata=COUNT)
    shared: int = field(metadata=COUNT)


@dataclass
class GroupKills:
    """Kills between the pilot and one flight group."""

    flight_group: int = field(metadata={"minimum": 0, "maximum": 47})
    name: str
    kills: int = field(metadata=COUNT)
    shared: int = field(metadata=COUNT)


@dataclass
class DebriefDetail:
    """The local pilot's own tables; the rows hold a number other than 0."""

    shots: int = field(metadata=COUNT)
    hits: int = field(metadata=COUNT)
    warheads: int = field(metadata=COUNT)
    warhead_hits: int = field(metadata=COUNT)
    losses: int = field(metadata=COUNT)
    losses_to_collisions: int = field(metadata=COUNT)
    losses_to_starships: int = field(metadata=COUNT)
    losses_to_mines: int = field(metadata=COUNT)
    friendlies_killed: int = field(metadata=COUNT)
    special_inspected: int = field(metadata=COUNT)
    by_craft: list[CraftKills]
    on_pilot_ranks: list[RankKills]
    on_ai_ranks: list[RankKills]
    killed_by_pilot_ranks: list[RankTimes]
    killed_by_ai_ranks: list[RankTimes]
    on_players: list[PlayerKills]
    from_players: list[PlayerKills]
    on_flight_groups: list[GroupKills]
    from_flight_groups: list[GroupKills]


@dataclass
class Debrief:
    """One flown battle's debriefing: what was flown, and every number with its name.

    ``players``, ``teams`` and ``detail`` are empty and null for a game build
    whose result does not carry them; ``local_entry`` is then -1.
    """

    result_id: int = field(metadata=ID_RANGE)
    ended_at: str
    mission_type: MissionTypeName
    side: SideName
    mission: str
    title: str
    pilot: str
    outcome: OutcomeName
    score: int
    kills: int = field(metadata=COUNT)
    losses: int = field(metadata=COUNT)
    goals_met_seconds: int = field(metadata=COUNT)
    local_entry: int = field(metadata={"minimum": -1, "maximum": 7})
    players: list[DebriefPlayer]
    teams: list[DebriefTeam]
    detail: DebriefDetail | None


@dataclass
class ResultSummary:
    """One kept debriefing in a line: when, which mission, how it ended."""

    result_id: int = field(metadata=ID_RANGE)
    ended_at: str
    title: str
    outcome: OutcomeName
    score: int


@dataclass
class ResultsListResult:
    """The kept debriefings, the newest first."""

    results: list[ResultSummary]


@dataclass
class ResultsGetResult:
    """The kept debriefing asked for; null when it is not kept."""

    debrief: Debrief | None


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
    """Tell every open page to show one mission of the network menus or a solo list."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["page.show_mission"]
    args: ShowMissionArgs | ShowSoloMissionArgs


@dataclass
class BriefingGetRequest:
    """Ask for the briefing bundle of one mission of the network menus or a solo list."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["briefing.get"]
    args: BriefingGetArgs | BriefingGetSoloArgs


@dataclass
class LaunchMenuRequest:
    """Ask for the solo list of a kind of battle and side."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["launch.menu"]
    args: SoloMenuArgs


@dataclass
class PilotsListRequest:
    """Ask for the pilots the game has."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["pilots.list"]
    args: NoArgs


@dataclass
class LaunchSoloRequest:
    """Fly one solo battle in the background, if every value is on the lists."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["launch.solo"]
    args: LaunchSoloArgs


@dataclass
class LaunchCancelRequest:
    """Stop the battle that is flying."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["launch.cancel"]
    args: LaunchCancelArgs


@dataclass
class OptionsGetRequest:
    """Ask for a player's game options."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["options.get"]
    args: OptionsGetArgs


@dataclass
class OptionsSetRequest:
    """Set one game option of a player."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["options.set"]
    args: OptionsSetArgs


@dataclass
class ResultsListRequest:
    """Ask for the kept debriefings, one line each."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["results.list"]
    args: NoArgs


@dataclass
class ResultsGetRequest:
    """Ask for one kept debriefing."""

    id: int = field(metadata=ID_RANGE)
    command: Literal["results.get"]
    args: ResultsGetArgs


REQUEST_CLASSES = (
    HelloRequest,
    InstallStatusRequest,
    MissionsListRequest,
    SettingsGetRequest,
    SettingsSetRequest,
    ShowMissionRequest,
    BriefingGetRequest,
    LaunchMenuRequest,
    PilotsListRequest,
    LaunchSoloRequest,
    LaunchCancelRequest,
    OptionsGetRequest,
    OptionsSetRequest,
    ResultsListRequest,
    ResultsGetRequest,
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
        | SoloMenuResult
        | PilotsListResult
        | LaunchSoloResult
        | LaunchCancelResult
        | OptionsResult
        | ResultsListResult
        | ResultsGetResult
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
    """What the launcher tells a page that just connected.

    ``pilot`` is the name of the linked Discord account's pilot, null when no
    account is linked or that account has no readable pilot.
    """

    launcher_version: str
    install_found: bool
    pilot: str | None


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
class ShownSoloMission:
    """The solo mission a page is told to show: its side and file, as the solo list gives them."""

    mission_type: MissionTypeName
    side: PilotSideName
    file: str
    title: str


@dataclass
class ShowMissionPush:
    """Sent to every open page when a mission is to be shown."""

    event: Literal["page.show_mission"]
    data: ShownMission | ShownSoloMission


@dataclass
class LaunchStartedData:
    """The battle that started, as a page shows it."""

    launch_id: int = field(metadata={"minimum": 1, "maximum": MAX_ID})
    mission_type: MissionTypeName
    side: SideName
    mission: str
    title: str
    pilot: str


@dataclass
class LaunchStartedPush:
    """Sent to every open page when a battle starts."""

    event: Literal["launch.started"]
    data: LaunchStartedData


@dataclass
class LaunchEndedData:
    """How the battle ended: its status, the one line that says so, and a flown battle's debriefing."""

    launch_id: int = field(metadata={"minimum": 1, "maximum": MAX_ID})
    status: LaunchStatus
    line: str
    debrief: Debrief | None


@dataclass
class LaunchEndedPush:
    """Sent to every open page when a battle ends."""

    event: Literal["launch.ended"]
    data: LaunchEndedData


@dataclass
class OptionsChangedPush:
    """Sent to every open page after a player's game options changed."""

    event: Literal["options.changed"]
    data: OptionsResult


@dataclass
class PilotChangedData:
    """The linked account's pilot name now; null when there is none."""

    pilot: str | None


@dataclass
class PilotChangedPush:
    """Sent to every open page after the bot makes or renames the linked account's pilot."""

    event: Literal["pilot.changed"]
    data: PilotChangedData


MESSAGE_CLASSES = (
    *REQUEST_CLASSES,
    OkReply,
    ErrorReply,
    StatusPush,
    SettingsChangedPush,
    ShowMissionPush,
    LaunchStartedPush,
    LaunchEndedPush,
    OptionsChangedPush,
    PilotChangedPush,
)

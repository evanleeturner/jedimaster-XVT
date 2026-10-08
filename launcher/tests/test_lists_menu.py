"""Mission menus: the reader, the game's view for each view, the type table.

Purpose:
    Prove every menu rule of the notes on synthetic menus: comments and
    sections where an entry may start, three-line entries taken as they
    come, blank lines as id 0, a cut-off entry, the ``&`` and ``*`` markers,
    lowercasing, and availability for a network game and each solo side.

Flow:
    Build menus with ``crlf``, read them, derive the view, inspect.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_lists_menu.py``
"""

from __future__ import annotations

import logging

import pytest
from listdata import crlf

from jedimaster.lists import game
from jedimaster.lists import menu_view
from jedimaster.lists import read_menu
from jedimaster.lists.model import MenuComment
from jedimaster.lists.model import MenuEntry
from jedimaster.lists.model import MenuSection

logger = logging.getLogger(__name__)

MENU = crlf(
    "1",
    "LOOSE.TIE",
    "Before Any Section",
    "//",
    "[First Group]",
    "// a note",
    "2",
    "Mixed.Tie",
    "Kept Title  ",
    "[Second]",
    "3",
    "& NEVER.TIE",
    "Locked",
    "4",
    "* 7 9 STAR.TIE",
    "Starred",
)


def entries(menu):
    return [item for item in menu.items if isinstance(item, MenuEntry)]


def test_items_in_file_order():
    menu = read_menu(MENU)
    kinds = [item.kind for item in menu.items]
    assert kinds == [
        "entry",
        "comment",
        "section",
        "comment",
        "entry",
        "section",
        "entry",
        "entry",
    ]
    assert menu.size == len(MENU) and not menu.end_mark
    assert isinstance(menu.items[1], MenuComment) and menu.items[1].line.line == 4


def test_section_name_drops_the_bracket_and_the_last_character():
    menu = read_menu(crlf("[Name]", "[Odd] ", "[", "[x"))
    names = [item.name for item in menu.items if isinstance(item, MenuSection)]
    assert names == ["Name", "Odd]", "", ""]


def test_entry_values_and_lines_kept_raw():
    first, second, third, fourth = entries(read_menu(MENU))
    assert (first.id, first.file_name, first.title_line.text) == (
        1,
        "LOOSE.TIE",
        "Before Any Section",
    )
    assert second.id_line.line == 7 and second.file_line.line == 8
    assert second.title_line.text == "Kept Title  "
    assert (third.marker, third.file_name) == ("&", "NEVER.TIE")
    assert (fourth.marker, fourth.star_texts, fourth.star_numbers) == (
        "*",
        ["7", "9"],
        [7, 9],
    )
    assert fourth.file_name == "STAR.TIE"


def test_comment_and_section_lines_are_taken_inside_an_entry():
    menu = read_menu(crlf("5", "//", "[Title]", "6", "b.tie", "Two"))
    first, second = entries(menu)
    assert first.file_name == "//" and first.title_line.text == "[Title]"
    assert second.id == 6
    assert all(not isinstance(i, MenuSection) for i in menu.items)


def test_blank_line_reads_as_id_zero_and_ids_read_like_atoi():
    menu = read_menu(crlf("", "a.tie", "A", " 12abc", "b.tie", "B", "x", "c.tie", "C"))
    assert [e.id for e in entries(menu)] == [0, 12, 0]
    assert entries(menu)[1].id_line.text == " 12abc"


def test_entry_cut_off_by_the_end_is_kept_raw_and_dropped_by_the_game():
    for data in (crlf("1", "a.tie", "A", ""), crlf("1", "a.tie", "A", "2", "b.tie")):
        menu = read_menu(data)
        last = entries(menu)[-1]
        assert not last.complete and last.title_line is None
        view = menu_view(menu, "melee", "network")
        assert [e.id for e in view.entries] == [1]


def test_end_mark_and_missing_last_line_ending():
    menu = read_menu(crlf("1", "a.tie", "A", end_mark=True))
    assert menu.end_mark and entries(menu)[0].complete
    menu = read_menu(crlf("1", "a.tie", "Last", final=False))
    assert entries(menu)[0].title_line.text == "Last"
    assert entries(menu)[0].title_line.ending == ""


def test_star_line_without_its_name():
    (entry,) = entries(read_menu(crlf("1", "* 3", "T")))
    assert entry.star_texts == ["3"] and entry.file_name is None
    view = menu_view(read_menu(crlf("1", "* 3", "T")), "training", "network")
    assert view.entries[0].file == ""


def test_game_view_lowercases_and_resolves_markers():
    view = menu_view(read_menu(MENU), "training", "network")
    assert [(e.section, e.id, e.available, e.file, e.title) for e in view.entries] == [
        ("", 1, True, "loose.tie", "Before Any Section"),
        ("First Group", 2, True, "mixed.tie", "Kept Title  "),
        ("Second", 3, False, "never.tie", "Locked"),
        ("Second", 4, False, "star.tie", "Starred"),
    ]
    assert view.game_path == "train\\mission.lst"


@pytest.mark.parametrize(
    ("view", "flown", "available"),
    [
        ("network", {}, False),
        ("network", {"rebel": {4}}, True),
        ("network", {"imperial": {4}}, True),
        ("rebel", {"rebel": {4}}, True),
        ("rebel", {"imperial": {4}}, False),
        ("imperial", {"imperial": {4}}, True),
        ("imperial", {"rebel": {4}, "imperial": {5}}, False),
    ],
)
def test_star_entry_needs_its_id_flown(view, flown, available):
    menu = read_menu(MENU)
    starred = menu_view(menu, "training", view, flown=flown).entries[3]
    assert starred.available is available
    never = menu_view(menu, "training", view, flown={"rebel": {3}, "imperial": {3}})
    assert never.entries[2].available is False


@pytest.mark.parametrize(
    ("mission_type", "network", "rebel", "imperial"),
    [
        ("training", "train\\mission.lst", "train\\rebel.lst", "train\\imperial.lst"),
        ("melee", "melee\\mission.lst", "melee\\mission.lst", "melee\\mission.lst"),
        (
            "tournament",
            "tourn\\mission.lst",
            "tourn\\mission.lst",
            "tourn\\mission.lst",
        ),
        ("combat", "combat\\mission.lst", "combat\\rebel.lst", "combat\\imperial.lst"),
        ("battle", "battle\\mission.lst", "battle\\mission.lst", "battle\\mission.lst"),
        (
            "campaign",
            "campaign\\mission.lst",
            "campaign\\rebel.lst",
            "campaign\\imperial.lst",
        ),
    ],
)
def test_menu_file_per_type_and_view(mission_type, network, rebel, imperial):
    paths = [game.menu_game_path(mission_type, v) for v in game.VIEWS]
    assert paths == [network, rebel, imperial]


def test_unknown_type_or_view_refused():
    menu = read_menu(MENU)
    with pytest.raises(ValueError):
        menu_view(menu, "skirmish", "network")
    with pytest.raises(ValueError):
        menu_view(menu, "training", "solo")
    assert game.sequence_game_path("battle", "b1.lst") == "battle\\b1.lst"
    assert [t.number for t in game.MISSION_TYPES.values()] == [0, 1, 2, 3, 4, 5]

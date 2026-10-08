"""Rendering the game's view in the answer sheets' format, and the raw dump.

Purpose:
    Prove each kind's sheet text line for line on synthetic lists: the
    header lines, the field order and names, the quoting, the final
    newline; and the reader's own dump.

Flow:
    Read synthetic lists, derive the view, render, compare with the text
    expected.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_lists_render.py``
"""

from __future__ import annotations

import logging

from listdata import crlf

from jedimaster.lists import awards_view
from jedimaster.lists import Bitmap
from jedimaster.lists import cutscenes_view
from jedimaster.lists import images_view
from jedimaster.lists import menu_view
from jedimaster.lists import read_awards
from jedimaster.lists import read_cutscenes
from jedimaster.lists import read_images
from jedimaster.lists import read_menu
from jedimaster.lists import read_sequence
from jedimaster.lists import read_ships
from jedimaster.lists import read_sounds
from jedimaster.lists import render_awards
from jedimaster.lists import render_cutscenes
from jedimaster.lists import render_images
from jedimaster.lists import render_menu
from jedimaster.lists import render_raw
from jedimaster.lists import render_sequence
from jedimaster.lists import render_ships
from jedimaster.lists import render_sounds
from jedimaster.lists import sequence_view
from jedimaster.lists import ships_view
from jedimaster.lists import sounds_view

logger = logging.getLogger(__name__)


def test_menu_sheet():
    menu = read_menu(crlf("[Group]", "4", "& A.TIE", 'Say "hi"', "5", "b.tie", "Plain"))
    text = render_menu(menu_view(menu, "melee", "network"), "melee\\mission.lst", "m/m")
    assert text == (
        "kind menu\n"
        'name "melee\\\\mission.lst"\n'
        'file "m/m"\n'
        "count 2\n"
        'entry 0 id=4 unavailable=1 section="Group" file="a.tie" description="Say \\"hi\\""\n'
        'entry 1 id=5 unavailable=0 section="Group" file="b.tie" description="Plain"\n'
    )


def test_sequence_sheet():
    seq = sequence_view(read_sequence(crlf("1", "A.tie", "Text", final=False)))
    text = render_sequence(
        seq, "tourn\\t.lst", "tourn/t.lst", ("tourn\\mission.lst", 2, 7)
    )
    assert text == (
        "kind sequence\n"
        'name "tourn\\\\t.lst"\n'
        'file "tourn/t.lst"\n'
        'menu "tourn\\\\mission.lst" entry=2 id=7\n'
        'description "Text"\n'
        'count_line "1\\n" count=1\n'
        'mission 0 line="A.tie\\n"\n'
    )
    assert "menu " not in render_sequence(seq, "n", "f")


def test_images_sheet():
    images = read_images(crlf("//", "b.bmp two 1", "a.bmp one 0", end_mark=True))
    view = images_view(images, {"a.bmp": Bitmap(3, 4), "b.bmp": Bitmap(5, 6)})
    assert render_images(view, "n", "f") == (
        'kind images\nname "n"\nfile "f"\n'
        "result 0\n"
        "count 2\n"
        'image 0 name="one" width=3 height=4 compressed=0\n'
        'image 1 name="two" width=5 height=6 compressed=1\n'
    )


def test_ships_sheet():
    view = ships_view(read_ships(crlf("x\\A.OPT 2", "x\\b.opt 20")))
    assert render_ships(view, "n", "f") == (
        'kind ships\nname "n"\nfile "f"\n'
        "count 2\n"
        'ship 0 model="x\\\\A.opt" type=2\n'
        'ship 1 model="x\\\\b.opt" type=20\n'
        "type_to_ship 0 0 0 2 3 4 5 6 7 0 0 0 0 0 8 0 9 0\n"
    )


def test_sounds_sheet():
    sounds = read_sounds(crlf("//", "b.wav two", "a.wav one"))
    assert render_sounds(sounds_view(sounds, loadable=()), "n", "f") == (
        'kind sounds\nname "n"\nfile "f"\nresult 1\ncount 0\n'
    )
    assert render_sounds(sounds_view(sounds), "n", "f").endswith(
        'count 2\nsound 0 name="one" file="a.wav"\nsound 1 name="two" file="b.wav"\n'
    )


def test_cutscenes_sheet():
    view = cutscenes_view(read_cutscenes(crlf("1", "mov", "3 1 9", "th", "Words here")))
    assert render_cutscenes(view, "n", "f") == (
        'kind cutscenes\nname "n"\nfile "f"\nresult 1\ncount 1\n'
        'cutscene 0 movie="mov" campaign=3 after_debriefing=1 mission=9 '
        'thumbnail="th" description="Words here"\n'
    )


def test_awards_sheet():
    names = [f"m{i}" for i in range(15)] + [f"s{i}" for i in range(15)]
    view = awards_view(read_awards(crlf("1", "4", "big", *names)))
    multi = " ".join(f'"m{i}"' for i in range(15)) + ' ""'
    single = " ".join(f'"s{i}"' for i in range(15)) + ' ""'
    assert render_awards(view, "n", "f") == (
        'kind awards\nname "n"\nfile "f"\nresult 1\ncount 1\n'
        'award 0 campaign=4 main="big"\n'
        f"award 0 multiplayer {multi}\n"
        f"award 0 singleplayer {single}\n"
    )


def test_raw_dump_prints_values_as_written():
    text = render_raw(read_menu(crlf("//", "[S]", "1", "* 2 3 A.TIE", "T", "9")))
    assert text.startswith(
        'size 31\nend_mark 0\nitems 4\nitems 0 line=1:"//"+"\\x0d\\n"'
    )
    assert 'star_texts=["2" "3"] star_numbers=[2 3] file_name="A.TIE"' in text
    assert 'items 3 id_line=6:"9"+"\\x0d\\n" id=9 file_line=- title_line=-' in text
    assert text.endswith('kind "menu"\n')
    images = render_raw(read_images(crlf("//", "a.bmp one 1")))
    assert 'groups 0 bitmap=2:"a.bmp" name=2:"one" flag=2:"1" flag_value=1' in images
    assert "unread []" in images

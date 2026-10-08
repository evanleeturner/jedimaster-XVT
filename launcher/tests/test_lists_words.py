"""The word lists: image lists, the ship list and the sound list.

Purpose:
    Prove the by-word reading (line breaks and tabs do not matter), the
    skipped first line, the ways a read ends (end of file, the 0x1A mark, a
    short group, a flag or type that is not a number), repeated names,
    missing bitmaps, the sorted tables, the ``opt`` rule and the ship
    table's slots, overwrite and type limit.

Flow:
    Build lists with ``crlf`` or raw bytes, read, derive the view, inspect.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_lists_words.py``
"""

from __future__ import annotations

import logging

from listdata import crlf

from jedimaster.lists import Bitmap
from jedimaster.lists import images_view
from jedimaster.lists import read_images
from jedimaster.lists import read_ships
from jedimaster.lists import read_sounds
from jedimaster.lists import ships_view
from jedimaster.lists import sounds_view

logger = logging.getLogger(__name__)

IMAGES = crlf(
    "a.bmp first 1 // the first line is skipped",
    "dir\\b.bmp zeta 1",
    "dir\\c.bmp\talpha\t0 dir\\d.bmp",
    "Beta",
    "1",
    "dir\\e.bmp zeta 0",
    "dir\\gone.bmp gamma 1",
)
BITMAPS = {
    "dir\\b.bmp": Bitmap(10, 20),
    "dir\\c.bmp": Bitmap(3, 4),
    "dir\\d.bmp": Bitmap(5, 6, compresses=False),
    "dir\\e.bmp": Bitmap(7, 8),
}


def test_image_groups_read_by_word():
    images = read_images(IMAGES)
    assert images.first_line.text.startswith("a.bmp first 1")
    triples = [(g.bitmap.text, g.name.text, g.flag_value) for g in images.groups]
    assert triples == [
        ("dir\\b.bmp", "zeta", 1),
        ("dir\\c.bmp", "alpha", 0),
        ("dir\\d.bmp", "Beta", 1),
        ("dir\\e.bmp", "zeta", 0),
        ("dir\\gone.bmp", "gamma", 1),
    ]
    assert [g.bitmap.line for g in images.groups] == [2, 3, 3, 6, 7]
    assert images.groups[2].flag.line == 5
    assert [g.repeat_of for g in images.groups] == [None, None, None, 0, None]
    assert images.end == "eof" and images.unread == []


def test_image_table_sorted_first_name_stays_missing_skipped():
    view = images_view(read_images(IMAGES), BITMAPS)
    assert [(i.name, i.bitmap, i.width, i.height) for i in view.images] == [
        ("Beta", "dir\\d.bmp", 5, 6),
        ("alpha", "dir\\c.bmp", 3, 4),
        ("zeta", "dir\\b.bmp", 10, 20),
    ]
    assert view.registered == [0, 1, 2]
    assert view.repeats == [3] and view.missing == [4]
    assert view.result == 1


def test_compressed_needs_the_flag_and_a_bitmap_that_shrinks():
    view = images_view(read_images(IMAGES), BITMAPS)
    assert {i.name: i.compressed for i in view.images} == {
        "Beta": False,
        "alpha": False,
        "zeta": True,
    }
    assert [i.flag for i in view.images] == [1, 0, 1]


def test_only_flag_one_asks_for_compression():
    data = crlf("//", "a.bmp two 2", "b.bmp minus -1", "c.bmp one 1", "d.bmp zero 0")
    images = read_images(data)
    assert [g.flag_value for g in images.groups] == [2, -1, 1, 0]
    assert [g.flag.text for g in images.groups] == ["2", "-1", "1", "0"]
    sizes = {f"{c}.bmp": Bitmap(1, 1) for c in "abcd"}
    view = images_view(images, sizes)
    assert {i.name: i.compressed for i in view.images} == {
        "minus": False,
        "one": True,
        "two": False,
        "zero": False,
    }
    assert {i.name: i.flag for i in view.images}["two"] == 2


def test_missing_bitmap_does_not_register_its_name():
    data = crlf("//", "x.bmp same 1", "y.bmp same 1")
    view = images_view(read_images(data), {"y.bmp": Bitmap(1, 1)})
    assert [(i.name, i.bitmap) for i in view.images] == [("same", "y.bmp")]
    assert view.missing == [0] and view.repeats == []


def test_image_read_ends():
    marked = read_images(crlf("//", "x.bmp one 1", end_mark=True))
    assert marked.end == "mark" and [w.text for w in marked.unread] == ["\x1a"]
    assert len(marked.groups) == 1 and marked.end_mark
    assert images_view(marked, {"x.bmp": Bitmap(1, 1)}).result == 0
    short = read_images(crlf("//", "x.bmp one 1", "y.bmp two"))
    assert short.end == "short" and [w.text for w in short.unread] == ["y.bmp", "two"]
    assert images_view(short, {}).result == 0
    bad = read_images(crlf("//", "x.bmp one yes", "z.bmp three 1"))
    assert bad.end == "bad_number" and bad.groups == [] and len(bad.unread) == 6
    assert images_view(bad, {}).result == 0
    assert read_images(b"").end == "eof" and read_images(b"").first_line is None


SHIPS = crlf(
    "ships\\ONE.OPT 1 ships\\two.Opt\t2",
    "ships\\note.txt 3",
    "ships\\THREE.OPT 1",
    "ships\\FOUR.OPT 16 ships\\FIVE.OPT 17 ships\\SIX.OPT -1",
    "ships\\SEVEN.OPT 14",
)


def test_ship_pairs_from_the_first_word():
    ships = read_ships(SHIPS)
    assert [(p.model.text, p.type_value) for p in ships.pairs][:3] == [
        ("ships\\ONE.OPT", 1),
        ("ships\\two.Opt", 2),
        ("ships\\note.txt", 3),
    ]
    assert len(ships.pairs) == 8 and ships.end == "eof"
    assert ships.pairs[2].model.line == 2


def test_ship_view_keeps_opt_models_with_the_tail_lowercased():
    view = ships_view(read_ships(SHIPS))
    assert [s.model for s in view.ships] == [
        "ships\\ONE.opt",
        "ships\\two.opt",
        "ships\\THREE.opt",
        "ships\\FOUR.opt",
        "ships\\FIVE.opt",
        "ships\\SIX.opt",
        "ships\\SEVEN.opt",
    ]
    assert [s.pair for s in view.ships] == [0, 1, 3, 4, 5, 6, 7]


def test_ship_type_table_overwrite_and_limit():
    table = ships_view(read_ships(SHIPS)).type_to_ship
    assert len(table) == 18
    assert table[1] == 2  # the later ship of type 1 overwrote the first
    assert table[2] == 1 and table[16] == 3 and table[14] == 6
    assert table[17] == 0  # type 17 fills no slot
    assert table[0] == 0 and table[3] == 2  # the skipped pair fills nothing


START = [0, 0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 8, 0, 9, 0]


def test_ship_table_starting_values():
    assert ships_view(read_ships(b"")).type_to_ship == START


def test_untouched_slot_keeps_its_start_and_named_slots_change():
    table = ships_view(read_ships(crlf("a.opt 14", "b.opt 9"))).type_to_ship
    assert table[14] == 0 and table[9] == 1  # named slots take positions
    assert table[4] == 3 and table[16] == 9  # untouched slots keep their start
    assert table[:4] == START[:4] and table[15] == START[15]


def test_ship_type_overwrite_and_types_17_and_up():
    pairs = crlf("a.opt 6", "b.opt 6", "c.opt 17", "d.opt 99", "e.opt 16")
    table = ships_view(read_ships(pairs)).type_to_ship
    assert table[6] == 1  # the later ship of type 6 overwrote the first
    assert table[17] == START[17] and table[16] == 4
    assert [t for i, t in enumerate(table) if i not in (6, 16)] == [
        s for i, s in enumerate(START) if i not in (6, 16)
    ]


def test_ship_name_cut_to_63_characters():
    long_name = "d\\" + "n" * 70 + ".OPT"
    (ship,) = ships_view(read_ships(crlf(f"{long_name} 1"))).ships
    assert ship.model == long_name[:63] and len(ship.model) == 63


def test_ship_read_ends():
    marked = read_ships(crlf("a.opt 1", end_mark=True))
    assert marked.end == "mark" and len(marked.pairs) == 1
    bad = read_ships(crlf("a.opt 1", "b.opt two", "c.opt 3"))
    assert bad.end == "bad_number" and [w.text for w in bad.unread][:2] == [
        "b.opt",
        "two",
    ]
    short = read_ships(crlf("a.opt 1 b.opt"))
    assert short.end == "short" and [w.text for w in short.unread] == ["b.opt"]


SOUNDS = crlf(
    "// first line skipped 1",
    "fx\\b.wav beep",
    "fx\\a.wav\tAlarm fx\\c.wav",
    "beep",
    "fx\\d.wav Zip",
    final=False,
)


def test_sound_pairs_and_repeats():
    sounds = read_sounds(SOUNDS)
    assert sounds.first_line.text == "// first line skipped 1"
    assert [(p.wav.text, p.name.text) for p in sounds.pairs] == [
        ("fx\\b.wav", "beep"),
        ("fx\\a.wav", "Alarm"),
        ("fx\\c.wav", "beep"),
        ("fx\\d.wav", "Zip"),
    ]
    assert [p.repeat_of for p in sounds.pairs] == [None, None, 0, None]
    assert sounds.end == "eof"


def test_sound_table_sorted_first_stays_failed_loads_skipped():
    view = sounds_view(read_sounds(SOUNDS))
    assert [(s.name, s.wav) for s in view.sounds] == [
        ("Alarm", "fx\\a.wav"),
        ("Zip", "fx\\d.wav"),
        ("beep", "fx\\b.wav"),
    ]
    assert view.repeats == [2] and view.attempts == [0, 1, 3] and view.result == 1
    none_load = sounds_view(read_sounds(SOUNDS), loadable=())
    assert none_load.sounds == [] and none_load.failed == [0, 1, 2, 3]
    assert none_load.repeats == []  # nothing loaded, so no name repeats
    some = sounds_view(read_sounds(SOUNDS), loadable={"fx\\d.wav"})
    assert [s.name for s in some.sounds] == ["Zip"]


def test_sound_word_without_partner_fails():
    sounds = read_sounds(crlf("//", "a.wav one", "b.wav"))
    assert sounds.end == "short" and sounds_view(sounds).result == 0
    marked = read_sounds(crlf("//", "a.wav one", end_mark=True))
    assert marked.end == "short" and marked.end_mark
    assert [w.text for w in marked.unread] == ["\x1a"]
    view = sounds_view(marked)
    assert view.result == 0 and [s.name for s in view.sounds] == ["one"]
    paired = read_sounds(crlf("//", "\x1a two"))
    assert [(p.wav.text, p.name.text) for p in paired.pairs] == [("\x1a", "two")]

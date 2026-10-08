"""The game's view of the icon sheets and its drawing rules.

Purpose:
    Prove the 565 rule, the IFF table for every byte, the craft table's end,
    the icon's position for odd and even sizes, index 0 skipped, the tables'
    shape, and the view of a synthetic install: Balance of Power first with
    a sheet missing from its folder, or the base install alone.

Flow:
    Build a fake install under ``tmp_path`` with ``bmpdata``; build the
    view; draw icons at the package's own boxes.

Invariants:
    - No game data: sheets, palettes and names of files are the tests' own;
      the boxes are read from the package, never copied.

Call:
    ``pytest tests/test_icons_game.py``
"""

from __future__ import annotations

import logging

import pytest
from bmpdata import bmp
from bmpdata import icon_install
from bmpdata import ramp
from bmpdata import sheet
from bmpdata import warned
from iconinstall import CRAFT
from iconinstall import make_install
from iconinstall import NAMES
from iconinstall import SHEET_H
from iconinstall import SHEET_W

from jedimaster.icons import Box
from jedimaster.icons import box_for_craft
from jedimaster.icons import BOXES
from jedimaster.icons import color_565
from jedimaster.icons import CRAFT_BOXES
from jedimaster.icons import draw_icon
from jedimaster.icons import icons_view
from jedimaster.icons import sheet_for_iff
from jedimaster.icons.game import icon_origin

logger = logging.getLogger(__name__)


@pytest.mark.parametrize(
    ("rgb", "value"),
    [
        ((0, 0, 0), 0x0000),
        ((255, 255, 255), 0xFFFF),
        ((255, 0, 0), 0xF800),
        ((0, 255, 0), 0x07E0),
        ((0, 0, 255), 0x001F),
        ((7, 3, 7), 0x0000),
        ((8, 4, 8), 0x0821),
        ((248, 252, 248), 0xFFFF),
        ((247, 251, 247), 0xF7DE),
    ],
    ids=["black", "white", "red", "green", "blue", "below", "step", "top", "near-top"],
)
def test_565_rule(rgb, value):
    assert color_565(*rgb) == value


def test_iff_table_for_all_256_values():
    table = {0: "mapicon0", 1: "mapicon1", 2: "mapicon2", 3: "mapicon3"}
    table.update({4: "mapicon1", 5: "mapicon4"})
    for iff in range(256):
        assert sheet_for_iff(iff) == table.get(iff, "mapicon0"), iff


def test_craft_types_105_106_and_255(caplog):
    assert box_for_craft(105) == CRAFT_BOXES[105]
    assert "no icon" not in warned(caplog)
    assert box_for_craft(106) is None
    assert "craft type 106 has no icon" in warned(caplog)
    assert box_for_craft(255) is None
    assert box_for_craft(-1) is None


def test_tables_lie_inside_a_sheet_and_name_existing_boxes():
    assert len(BOXES) == 70
    assert len(CRAFT_BOXES) == 106
    for box in BOXES:
        assert 0 <= box.left <= box.right < SHEET_W, box
        assert 0 <= box.top <= box.bottom < SHEET_H, box
    assert all(0 <= b < len(BOXES) for b in CRAFT_BOXES)


@pytest.mark.parametrize(
    ("box", "origin"),
    [
        (Box(0, 0, 3, 5), (-2, -3)),
        (Box(0, 0, 4, 6), (-2, -3)),
        (Box(10, 20, 10, 20), (0, 0)),
        (Box(10, 20, 11, 21), (-1, -1)),
        (Box(5, 5, 13, 16), (-4, -6)),
    ],
    ids=["even", "odd", "one", "two", "odd-even"],
)
def test_position_for_odd_and_even_sizes(box, origin):
    assert icon_origin(box) == origin


def test_view_reads_balance_of_power_first_with_a_sheet_missing(tmp_path):
    root = make_install(tmp_path / "XvT")
    view = icons_view(root)
    assert list(view.sheets) == ["greyicon", *NAMES[:5]]
    where = {n: s.path.relative_to(root).as_posix() for n, s in view.sheets.items()}
    assert where["mapicon0"] == "BalanceOfPower/FRONTRES/s0.bmp"
    assert where["mapicon3"] == "frontres/S3.BMP"
    assert view.sheets["mapicon3"].file == "frontres/s3.bmp"
    assert view.sheets["mapicon1"].file == "BalanceOfPower/frontres/s1.bmp"
    assert view.sheets["mapicon1"].game_path == "frontres\\s1.bmp"
    assert view.list_file == "frontres/mapicons.lst"
    assert draw_icon(view, 3, CRAFT).pixels[0].index == 1
    assert draw_icon(view, 0, CRAFT).pixels[0].index == 2


def test_view_without_balance_of_power_reads_the_base_install(tmp_path):
    view = icons_view(make_install(tmp_path / "XvT"), balance_of_power=False)
    assert all("BalanceOfPower" not in str(s.path) for s in view.sheets.values())
    assert draw_icon(view, 0, CRAFT).pixels[0].index == 1


def test_view_keeps_a_compressed_image_without_pixels(tmp_path):
    view = icons_view(make_install(tmp_path / "XvT"))
    names = [i.name for i in view.images.images]
    assert names == ["greyicon", *NAMES[:5], "stars"]
    assert "stars" not in view.sheets
    assert view.sheets["mapicon0"].colors == [color_565(*ramp(i)) for i in range(256)]


def test_view_without_its_list_is_refused(tmp_path):
    root = icon_install(tmp_path / "XvT", {"frontres/other.lst": b""})
    with pytest.raises(FileNotFoundError, match="mapicons.lst"):
        icons_view(root)


def test_an_unreadable_sheet_is_left_out_with_a_warning(tmp_path, caplog):
    root = make_install(tmp_path / "XvT")
    (root / "BalanceOfPower/FRONTRES/s2.bmp").write_bytes(bmp(4, 1, b"")[:200])
    view = icons_view(root)
    assert "mapicon2" not in view.sheets
    assert "cannot read" in warned(caplog)
    assert draw_icon(view, 2, CRAFT) is None
    assert "not loaded" in warned(caplog)


def test_draw_skips_index_0_and_places_the_box_on_p(tmp_path):
    view = icons_view(make_install(tmp_path / "XvT"))
    box = BOXES[CRAFT_BOXES[CRAFT]]
    draw = draw_icon(view, 4, CRAFT)
    assert (draw.sheet, draw.box) == ("mapicon1", CRAFT_BOXES[CRAFT])
    x, y = icon_origin(box)
    assert (draw.x, draw.y) == (x, y)
    assert len(draw.pixels) == box.width * box.height - 1
    assert (x + 1, y) not in {(p.dx, p.dy) for p in draw.pixels}
    assert (draw.pixels[0].dx, draw.pixels[0].dy) == (x, y)
    assert draw.pixels[-1][:2] == (x + box.width - 1, y + box.height - 1)
    assert {p.color for p in draw.pixels} == {color_565(*ramp(2))}


def test_an_all_zero_box_draws_nothing(tmp_path):
    root = make_install(tmp_path / "XvT")
    (root / "frontres/S0.BMP").write_bytes(sheet(SHEET_W, SHEET_H, {}))
    view = icons_view(root, balance_of_power=False)
    draw = draw_icon(view, 0, CRAFT)
    assert draw is not None
    assert draw.pixels == []


def test_no_icon_for_a_craft_without_a_box(tmp_path, caplog):
    view = icons_view(make_install(tmp_path / "XvT"))
    assert draw_icon(view, 0, 106) is None
    assert "craft type 106" in warned(caplog)


def test_no_icon_when_the_box_lies_outside_the_sheet(tmp_path, caplog):
    root = make_install(tmp_path / "XvT")
    (root / "frontres/S0.BMP").write_bytes(sheet(8, 8, {}))
    view = icons_view(root, balance_of_power=False)
    craft = next(t for t, b in enumerate(CRAFT_BOXES) if BOXES[b].right >= 8)
    assert draw_icon(view, 0, craft) is None
    assert "outside sheet" in warned(caplog)

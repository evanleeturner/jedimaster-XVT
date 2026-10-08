"""The icon outputs: the three sheet forms, the dump, JSON, schema and PNG.

Purpose:
    Prove the renderers print the answer sheets' form line for line, the
    JSON export is valid against the published schema and the schema file
    matches its model, and a written PNG reads back to the palette's colors
    with index 0 transparent.

Flow:
    Build synthetic bitmaps and installs, render or export, read back.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_icons_output.py``
"""

from __future__ import annotations

import copy
import json
import logging
import struct
import zlib
from pathlib import Path

import jsonschema
import pytest
from bmpdata import bmp
from bmpdata import crlf_list
from bmpdata import end
from bmpdata import icon_install
from bmpdata import plain
from bmpdata import ramp
from bmpdata import run
from iconinstall import CRAFT
from iconinstall import make_install

from jedimaster.icons import BOXES
from jedimaster.icons import CRAFT_BOXES
from jedimaster.icons import IconDraw
from jedimaster.icons import IconPixel
from jedimaster.icons import icons_schema
from jedimaster.icons import icons_to_json
from jedimaster.icons import icons_view
from jedimaster.icons import picture_name
from jedimaster.icons import png_bytes
from jedimaster.icons import read_bmp
from jedimaster.icons import render_bmp
from jedimaster.icons import render_draws
from jedimaster.icons import render_sheets
from jedimaster.icons import render_tables
from jedimaster.icons.render import draw_lines

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "icons.schema.json"


@pytest.fixture(scope="module")
def validator():
    """Return a draft 2020-12 validator for the published schema file."""
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def small_install(root: Path, name: str = "mapicon0") -> Path:
    """Return an install with one 3 by 2 sheet ``name`` and a compressed image."""
    rows = [b"\x0a\x0b\x0c", b"\x00\xff\x01"]
    return icon_install(
        root,
        {
            "frontres/mapicons.lst": crlf_list(
                "//", f"frontres\\a.bmp {name} 0", "frontres\\b.bmp stars 1"
            ),
            "frontres/a.bmp": bmp(3, 2, plain(rows), compression=0),
            "frontres/b.bmp": bmp(8, 4, b""),
        },
    )


def test_render_sheets_prints_header_images_colors_and_rows(tmp_path):
    lines = render_sheets(icons_view(small_install(tmp_path / "XvT"))).splitlines()
    assert lines[:6] == [
        "kind sheets",
        'name "frontres\\\\mapicons.lst"',
        'file "frontres/mapicons.lst"',
        "result 0",
        "count 2",
        'image 0 name="mapicon0" width=3 height=2 compressed=0',
    ]
    colors = lines[6].split()
    assert colors[:3] == ["colors", "07e0", "07e1"]
    assert len(colors) == 257
    assert lines[7:] == [
        "row 0 0a0b0c",
        "row 1 00ff01",
        'image 1 name="stars" width=8 height=4 compressed=1',
    ]


def test_render_tables_prints_every_box_and_craft():
    lines = render_tables().splitlines()
    assert lines[:2] == ["kind tables", f"boxes {len(BOXES)}"]
    first = BOXES[0]
    assert lines[2] == (
        f"box 0 left={first.left} top={first.top} "
        f"right={first.right} bottom={first.bottom}"
    )
    assert lines[2 + len(BOXES)] == f"crafts {len(CRAFT_BOXES)}"
    assert lines[-1] == f"craft 105 icon={CRAFT_BOXES[105]}"
    assert len(lines) == 3 + len(BOXES) + len(CRAFT_BOXES)


def test_draw_lines_print_the_written_pixels_bounding_box():
    pixels = [IconPixel(-1, -1, 4, 0x1234), IconPixel(1, 0, 9, 0xABCD)]
    draw = IconDraw(3, 7, "mapicon3", 1, -2, -1, pixels)
    assert draw_lines(draw, 3, 7) == [
        "draw iff=3 craft=7 written=2",
        "at -1 -1 size 3 2",
        "row -1 1234 ---- ----",
        "row 0 ---- ---- abcd",
    ]
    assert draw_lines(None, 3, 106) == ["draw iff=3 craft=106 written=0"]
    empty = IconDraw(3, 7, "mapicon3", 1, -2, -1, [])
    assert draw_lines(empty, 3, 7) == ["draw iff=3 craft=7 written=0"]


def test_render_draws_runs_every_iff_and_craft(tmp_path):
    view = icons_view(make_install(tmp_path / "XvT"))
    lines = render_draws(view).splitlines()
    assert lines[0] == "kind draws"
    draws = [line for line in lines if line.startswith("draw ")]
    assert len(draws) == 9 * len(CRAFT_BOXES)
    assert draws[0] == "draw iff=0 craft=0 written=0"
    assert draws[-1] == "draw iff=255 craft=105 written=0"
    box = BOXES[CRAFT_BOXES[CRAFT]]
    written = box.width * box.height - 1
    index = lines.index(f"draw iff=3 craft={CRAFT} written={written}")
    assert lines[index + 1] == (
        f"at {-(box.width >> 1)} {-(box.height >> 1)} size {box.width} {box.height}"
    )


def test_render_bmp_prints_header_and_decoding_not_pixels():
    text = render_bmp(read_bmp(bmp(4, 1, run(6, 2) + end(), offset=60)))
    lines = text.splitlines()
    assert lines[0] == f"file_size {1078 + 4}"
    assert "pixel_offset 60" in lines
    assert "dropped 2" in lines
    assert "end_missing 0" in lines
    assert "offset_differs 1" in lines
    assert not any(line.startswith(("palette", "pixels")) for line in lines)


def test_export_validates_against_schema(tmp_path, validator):
    root = make_install(tmp_path / "XvT")
    for bop in (True, False):
        data = icons_to_json(icons_view(root, balance_of_power=bop))
        validator.validate(data)
        json.dumps(data)


def test_export_holds_sheets_tables_and_pictures(tmp_path):
    data = icons_to_json(icons_view(make_install(tmp_path / "XvT")))
    sheet = data["sheets"][1]
    assert sheet == {
        "name": "mapicon0",
        "color": "green",
        "game_path": "frontres\\s0.bmp",
        "file": "BalanceOfPower/FRONTRES/s0.bmp",
        "width": 320,
        "height": 200,
        "picture": "mapicon0.png",
    }
    assert data["list"] == {
        "game_path": "frontres\\mapicons.lst",
        "file": "frontres/MAPICONS.LST",
    }
    assert [s["color"] for s in data["sheets"]][0] == "grey"
    assert data["boxes"][0]["width"] == BOXES[0].width
    assert data["crafts"][105] == {"craft": 105, "box": CRAFT_BOXES[105]}
    assert (data["iffs"][4], data["iffs"][255], len(data["iffs"])) == (
        "mapicon1",
        "mapicon0",
        256,
    )
    unnamed = icons_to_json(icons_view(small_install(tmp_path / "s", "panel")))
    assert unnamed["sheets"][0]["color"] is None


def test_picture_names_are_file_safe():
    assert picture_name("mapicon0") == "mapicon0.png"
    assert picture_name("a b/c\\d") == "a_b_c_d.png"
    assert picture_name("") == "_.png"


def test_schema_file_matches_model():
    text = json.dumps(icons_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/icons.schema.json is stale: run python tools/gen_schema.py"
    )


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("format",), "jedimaster.lists"),
        (("iffs",), ["mapicon0"] * 255),
        (("sheets", 0, "extra"), 1),
        (("sheets", 0, "picture"), "../x.png"),
        (("sheets", 0, "width"), -1),
        (("sheets", 0, "color"), 3),
        (("boxes", 0, "left"), "6"),
        (("crafts", 0, "box"), None),
        (("transparent_index",), 1),
    ],
    ids=[
        "format",
        "iffs",
        "extra",
        "picture",
        "width",
        "color",
        "left",
        "box",
        "index",
    ],
)
def test_schema_rejects_bad_data(tmp_path, validator, path, value):
    data = copy.deepcopy(icons_to_json(icons_view(make_install(tmp_path / "XvT"))))
    target = data
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert list(validator.iter_errors(data)), path


def chunks(data: bytes) -> list[tuple[bytes, bytes]]:
    """Return a PNG's chunks as (type, body), checking each CRC."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    found, at = [], 8
    while at < len(data):
        (size,) = struct.unpack_from(">I", data, at)
        kind, body = data[at + 4 : at + 8], data[at + 8 : at + 8 + size]
        (crc,) = struct.unpack_from(">I", data, at + 8 + size)
        assert crc == zlib.crc32(kind + body)
        found.append((kind, body))
        at += 12 + size
    return found


def test_png_reads_back_to_the_palette_colors_with_index_0_clear(tmp_path):
    image = read_bmp(
        bmp(3, 2, plain([b"\x00\x01\x02", b"\x03\x00\xff"]), compression=0)
    )
    parts = chunks(png_bytes(image))
    assert [kind for kind, _ in parts] == [b"IHDR", b"IDAT", b"IEND"]
    assert struct.unpack(">IIBBBBB", parts[0][1]) == (3, 2, 8, 6, 0, 0, 0)
    raw = zlib.decompress(parts[1][1])
    assert len(raw) == 2 * (1 + 3 * 4)
    assert (raw[0], raw[13]) == (0, 0)
    pixels = [raw[1:13][i : i + 4] for i in range(0, 12, 4)]
    pixels += [raw[14:26][i : i + 4] for i in range(0, 12, 4)]
    expected = [bytes((*ramp(i), 0 if i == 0 else 255)) for i in (0, 1, 2, 3, 0, 255)]
    assert pixels == expected


def test_png_of_an_empty_bitmap_is_refused():
    with pytest.raises(ValueError, match="no PNG"):
        png_bytes(read_bmp(bmp(0, 3, b"", compression=0)))

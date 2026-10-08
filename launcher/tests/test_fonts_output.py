"""The font outputs: the view, the sheets' form, atlases, JSON, schema, CLI.

Purpose:
    Prove the fonts view resolves the four fonts in any letter case,
    Balance of Power first; the renderer prints the font sheets' form line
    for line (glyph rows of ``#`` and ``.``, sample strings as 565 cells);
    an atlas PNG reads back to opaque white glyph pixels on a fully
    transparent ground at the places the JSON gives; the JSON validates
    against the published schema, which matches its model; and the
    ``fonts`` command line dumps and exports with the package's statuses.

Flow:
    Build synthetic fonts and installs with ``textdata``; render, export,
    inflate the PNG with ``zlib``, read back.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_fonts_output.py``
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
from textdata import font_bytes
from textdata import write_files

from jedimaster.__main__ import main
from jedimaster.fonts import atlas_png
from jedimaster.fonts import COLOR_CODES
from jedimaster.fonts import font_atlas
from jedimaster.fonts import fonts_schema
from jedimaster.fonts import fonts_to_json
from jedimaster.fonts import fonts_view
from jedimaster.fonts import read_font
from jedimaster.fonts import render_font

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "fonts.schema.json"
SHAPE = {65: (3, [b"\x41\x02\x00", b"\x81\x00\x40"]), 66: (1, [b"\x02\x00", b""])}
"""Glyph 65: row 0 skips 1 and draws 2; row 1 draws 1. Glyph 66 reaches x 1."""


@pytest.fixture(scope="module")
def validator():
    """Return a draft 2020-12 validator for the published schema file."""
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def small_font(**options) -> bytes:
    return font_bytes(SHAPE, height=2, **options)


def make_install(root: Path) -> Path:
    files = {f"TIMES{n}.ABP": small_font(points=n) for n in (10, 12, 15, 20)}
    files["BalanceOfPower/times12.abp"] = small_font(points=99)
    return write_files(root, files)


def test_view_resolves_in_any_case_balance_of_power_first(tmp_path):
    root = make_install(tmp_path / "XvT")
    view = fonts_view(root)
    assert list(view.fonts) == ["font10", "font12", "font15", "font20"]
    assert view.fonts["font10"].file == "times10.abp"
    assert view.fonts["font12"].file == "BalanceOfPower/times12.abp"
    assert view.fonts["font12"].font.points == 99
    base = fonts_view(root, balance_of_power=False)
    assert base.fonts["font12"].font.points == 12


def test_view_keeps_a_missing_or_bad_font_as_an_error(tmp_path):
    root = write_files(tmp_path / "XvT", {"times10.abp": b"short"})
    view = fonts_view(root)
    assert view.fonts["font10"].font is None and view.fonts["font10"].error
    assert isinstance(view.fonts["font20"].error, FileNotFoundError)


def test_render_font_form():
    font = read_font(small_font(points=11, spacing=1, spare=3))
    lines = render_font(font, "f.abp", "dir/f.abp", [b"A\x02B", b""]).splitlines()
    assert lines[:6] == [
        "kind font",
        'name "f.abp"',
        'file "dir/f.abp"',
        "result 1",
        "points 11 in_use 1 spacing 1 field_60a 3 height 2",
        "color_codes " + " ".join(f"{c:04x}" for c in COLOR_CODES),
    ]
    at = lines.index("glyph 65 width=3 height=2 offset=12 clip=0 written=3")
    assert lines[at + 1 : at + 3] == ["row 0 .##", "row 1 #.."]
    at = lines.index("glyph 66 width=1 height=2 offset=28 clip=0 written=2")
    assert lines[at + 1 : at + 3] == ["row 0 ##", "row 1 .."]
    at = lines.index('text "A\\x02B" measure=5 clip=0 written=5 right=6 bottom=2')
    green = f"{COLOR_CODES[1]:04x}"
    assert lines[at + 1] == f"row 0 ---- ffff ffff ---- {green} {green}"
    assert lines[at + 2] == "row 1 ffff ---- ---- ---- ---- ----"
    assert lines[-1] == 'text "" measure=-1 clip=0 written=0 right=0 bottom=0'
    assert len([line for line in lines if line.startswith("glyph ")]) == 256
    assert lines[6] == "glyph 0 width=2 height=2 offset=0 clip=0 written=0"
    assert lines[7] == "row 0 .."


def png_pixels(data: bytes) -> tuple[int, int, list[bytes]]:
    """Return a PNG's width, height and RGBA pixels (filter 0 rows only)."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    at, idat, header = 8, b"", b""
    while at < len(data):
        (size,) = struct.unpack(">I", data[at : at + 4])
        kind, body = data[at + 4 : at + 8], data[at + 8 : at + 8 + size]
        header = body if kind == b"IHDR" else header
        idat += body if kind == b"IDAT" else b""
        at += 12 + size
    width, height = struct.unpack(">II", header[:8])
    raw = zlib.decompress(idat)
    stride = 1 + 4 * width
    pixels = []
    for y in range(height):
        row = raw[y * stride : (y + 1) * stride]
        assert row[0] == 0
        pixels += [row[1 + 4 * x : 5 + 4 * x] for x in range(width)]
    return width, height, pixels


def test_atlas_png_reads_back_white_glyphs_on_clear_ground():
    font = read_font(small_font())
    atlas = font_atlas(font)
    assert (atlas.cell_width, atlas.cell_height) == (3, 2)
    assert (atlas.width, atlas.height) == (48, 32)
    assert atlas.places[65] == (3, 8) and atlas.places[16] == (0, 2)
    width, height, pixels = png_pixels(atlas_png(atlas))
    assert (width, height) == (48, 32)
    ink = {i for i, p in enumerate(pixels) if p == b"\xff\xff\xff\xff"}
    assert {p for p in pixels} == {b"\xff\xff\xff\xff", b"\0\0\0\0"}
    x, y = atlas.places[65]
    assert {(y) * width + x + 1, y * width + x + 2, (y + 1) * width + x} <= ink
    assert len(ink) == 3 + 2


def test_export_validates_and_places_glyphs(tmp_path, validator):
    view = fonts_view(make_install(tmp_path / "XvT"))
    data = json.loads(json.dumps(fonts_to_json(view)))
    assert list(validator.iter_errors(data)) == []
    font = data["fonts"][0]
    assert (font["height"], font["spacing"], font["picture"]) == (2, 0, "times10.png")
    assert font["glyphs"][65] == {"code": 65, "x": 3, "y": 8, "width": 3, "height": 2}
    assert [c["color"] for c in font["text_colors"]] == list(COLOR_CODES[1:])
    assert data["reset_byte"] == 1


def test_export_of_a_missing_font_validates(tmp_path, validator):
    view = fonts_view(write_files(tmp_path / "XvT", {}))
    data = json.loads(json.dumps(fonts_to_json(view)))
    assert list(validator.iter_errors(data)) == []
    assert data["fonts"][0]["glyphs"] is None and data["fonts"][0]["error"]


def test_schema_file_matches_model():
    text = json.dumps(fonts_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/fonts.schema.json is stale: run python tools/gen_schema.py"
    )


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("fonts", 0, "glyphs", 0, "code"), 256),
        (("fonts", 0, "glyphs"), []),
        (("fonts", 0, "text_colors", 0, "byte"), 7),
        (("fonts", 0, "atlas", "columns"), 8),
        (("fonts", 0, "picture"), "x.bmp"),
        (("reset_byte",), 2),
        (("fonts", 0, "extra"), 1),
    ],
    ids=[
        "glyph-code",
        "glyph-count",
        "color-byte",
        "atlas-columns",
        "picture-name",
        "reset-byte",
        "font-extra",
    ],
)
def test_schema_rejects_bad_data(tmp_path, validator, path, value):
    view = fonts_view(make_install(tmp_path / "XvT"))
    data = copy.deepcopy(json.loads(json.dumps(fonts_to_json(view))))
    target = data
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert list(validator.iter_errors(data)), path


def test_fonts_dump(tmp_path, capsys):
    root = make_install(tmp_path / "XvT")
    path = str(root / "TIMES10.ABP")
    assert main(["fonts", "dump", path, "--text", "A\\x02B"]) == 0
    out = capsys.readouterr().out
    font = read_font(root / "TIMES10.ABP")
    assert out == render_font(font, "times10.abp", path, [b"A\x02B"])
    assert main(["fonts", "dump", "times15.abp", "--install", str(root)]) == 0
    assert "points 15 " in capsys.readouterr().out
    assert main(["fonts", "dump", "none.abp", "--install", str(root)]) == 2
    assert main(["fonts", "dump", path, "--text", "bad\\q"]) == 1
    bad = tmp_path / "bad.abp"
    bad.write_bytes(b"x")
    assert main(["fonts", "dump", str(bad)]) == 1


def test_fonts_export(tmp_path, capsys, validator):
    root = make_install(tmp_path / "XvT")
    out = tmp_path / "out"
    assert main(["fonts", "export", str(root), str(out)]) == 0
    assert "exported 4 font atlases" in capsys.readouterr().out
    assert sorted(p.name for p in out.glob("*.png")) == [
        f"times{n}.png" for n in (10, 12, 15, 20)
    ]
    data = json.loads((out / "fonts.json").read_text(encoding="utf-8"))
    assert list(validator.iter_errors(data)) == []
    assert main(["fonts", "export", str(root), str(out), "--no-balance-of-power"]) == 0
    data = json.loads((out / "fonts.json").read_text(encoding="utf-8"))
    assert data["balance_of_power"] is False and data["fonts"][1]["points"] == 12
    assert main(["fonts", "export", str(tmp_path / "none"), str(out)]) == 2
    blocked = tmp_path / "file"
    blocked.write_bytes(b"")
    assert main(["fonts", "export", str(root), str(blocked)]) == 1
    (root / "TIMES20.ABP").unlink()
    assert main(["fonts", "export", str(root), str(tmp_path / "o2")]) == 1

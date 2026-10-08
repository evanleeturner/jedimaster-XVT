"""The menu pictures: the list, each view's reading, the hashes, the outputs.

Purpose:
    Prove the pictures' list joins both menu folders (any letter case),
    lowercases the names, drops repeats and sorts letters and digits
    first; each view resolves every name (missing from the base view when
    only Balance of Power has it); the two FNV-1a hashes hold on known
    input; the sheet line, the JSON (valid against its schema, which
    matches its model), the PNG (read back with ``zlib``) and the
    ``pictures`` command line come out as they should.

Flow:
    Build bitmaps with ``bmpdata`` and installs with ``textdata``; view,
    render, export, read back.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_pictures.py``
"""

from __future__ import annotations

import copy
import json
import logging
from pathlib import Path

import jsonschema
import pytest
from bmpdata import bmp
from bmpdata import end
from bmpdata import plain
from bmpdata import ramp
from bmpdata import run
from bmpdata import warned
from test_fonts_output import png_pixels
from textdata import write_files

from jedimaster.__main__ import main
from jedimaster.icons import color_565
from jedimaster.icons import png_bytes
from jedimaster.pictures import colors_hash
from jedimaster.pictures import fnv1a64
from jedimaster.pictures import name_order
from jedimaster.pictures import picture_line
from jedimaster.pictures import picture_names
from jedimaster.pictures import pictures_schema
from jedimaster.pictures import pictures_to_json
from jedimaster.pictures import pictures_view
from jedimaster.pictures import pixels_hash
from jedimaster.pictures import png_names
from jedimaster.pictures import render_pictures

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "pictures.schema.json"
SMALL = bmp(3, 2, plain([b"\x00\x01\x02", b"\x03\x00\xff"]), compression=0)
PACKED = bmp(2, 1, run(2, 5) + end())


@pytest.fixture(scope="module")
def validator():
    """Return a draft 2020-12 validator for the published schema file."""
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def make_install(root: Path) -> Path:
    return write_files(
        root,
        {
            "FrontRes/B_X.BMP": SMALL,
            "FrontRes/a~1.bmp": SMALL,
            "FrontRes/ab.Bmp": PACKED,
            "FrontRes/notes.txt": b"not a picture",
            "FrontRes/shared.bmp": SMALL,
            "balanceofpower/FRONTRES/SHARED.BMP": PACKED,
            "balanceofpower/FRONTRES/only.bmp": SMALL,
        },
    )


def test_list_joins_both_folders_lowercased_without_repeats(tmp_path):
    names = picture_names(make_install(tmp_path / "XvT"))
    assert names == [
        "frontres\\a~1.bmp",
        "frontres\\ab.bmp",
        "frontres\\b_x.bmp",
        "frontres\\only.bmp",
        "frontres\\shared.bmp",
    ]
    assert picture_names(write_files(tmp_path / "Empty", {})) == []


def test_list_order_passes_over_punctuation():
    names = [
        "frontres\\ab.bmp",
        "frontres\\a_c.bmp",
        "frontres\\a-b.bmp",
        "frontres\\a1.bmp",
    ]
    assert sorted(names, key=name_order) == [
        "frontres\\a1.bmp",
        "frontres\\a-b.bmp",
        "frontres\\ab.bmp",
        "frontres\\a_c.bmp",
    ]
    assert sorted(["x\\a_b", "x\\a-b"], key=name_order) == ["x\\a-b", "x\\a_b"]


def test_each_view_resolves_every_name(tmp_path):
    root = make_install(tmp_path / "XvT")
    bop = {p.name: p for p in pictures_view(root).pictures}
    base = {p.name: p for p in pictures_view(root, balance_of_power=False).pictures}
    assert bop["frontres\\only.bmp"].status == "loaded"
    assert bop["frontres\\only.bmp"].file == "BalanceOfPower/frontres/only.bmp"
    assert base["frontres\\only.bmp"].status == "missing"
    assert bop["frontres\\shared.bmp"].bitmap.width == 2
    assert base["frontres\\shared.bmp"].bitmap.width == 3
    assert base["frontres\\b_x.bmp"].file == "frontres/b_x.bmp"


def test_a_refused_picture_is_not_loaded(tmp_path, caplog):
    caplog.set_level(logging.WARNING)
    root = write_files(tmp_path / "XvT", {"frontres/bad.bmp": b"BM" + b"\0" * 10})
    picture = pictures_view(root).pictures[0]
    assert picture.status == "not_loaded" and picture.bitmap is None
    assert picture_line(picture) == 'picture "frontres\\\\bad.bmp" not_loaded'
    assert "not loaded" in warned(caplog)


def test_hashes_on_known_input(tmp_path):
    assert fnv1a64(b"") == 0xCBF29CE484222325
    assert fnv1a64(b"a") == 0xAF63DC4C8601EC8C
    assert fnv1a64(b"foobar") == 0x85944171F73967E8
    assert colors_hash([0x1234, 0xABCD]) == fnv1a64(b"\x34\x12\xcd\xab")
    root = write_files(tmp_path / "XvT", {"frontres/s.bmp": SMALL})
    picture = pictures_view(root).pictures[0]
    assert pixels_hash(picture.bitmap) == fnv1a64(b"\x00\x01\x02\x03\x00\xff")
    assert picture.colors == [color_565(*ramp(i)) for i in range(256)]
    assert colors_hash(picture.colors) == fnv1a64(
        b"".join(color_565(*ramp(i)).to_bytes(2, "little") for i in range(256))
    )


def test_render_lines(tmp_path):
    root = make_install(tmp_path / "XvT")
    lines = render_pictures(pictures_view(root, balance_of_power=False)).splitlines()
    small = pictures_view(root).pictures[0].bitmap
    assert lines[0] == (
        'picture "frontres\\\\a~1.bmp" file="frontres/a~1.bmp" width=3 height=2 '
        f"pixels={pixels_hash(small):016x} "
        f"colors={colors_hash([color_565(*ramp(i)) for i in range(256)]):016x}"
    )
    assert lines[3] == 'picture "frontres\\\\only.bmp" missing'
    assert render_pictures(pictures_view(write_files(tmp_path / "E", {}))) == "\n"


def test_png_names_are_safe_and_unique(tmp_path):
    root = write_files(
        tmp_path / "XvT",
        {"frontres/a b.bmp": SMALL, "frontres/a_b.bmp": SMALL, "frontres/z.bmp": b"x"},
    )
    names = png_names(pictures_view(root))
    assert names == {"frontres\\a b.bmp": "a_b.png", "frontres\\a_b.bmp": "a_b-2.png"}


def test_export_validates_and_gives_index_0(tmp_path, validator):
    root = make_install(tmp_path / "XvT")
    view = pictures_view(root, balance_of_power=False)
    data = json.loads(json.dumps(pictures_to_json(view, png_names(view))))
    assert list(validator.iter_errors(data)) == []
    first = data["pictures"][0]
    red, green, blue = ramp(0)
    assert first == {
        "name": "frontres\\a~1.bmp",
        "status": "loaded",
        "file": "FrontRes/a~1.bmp",
        "width": 3,
        "height": 2,
        "picture": "a~1.png",
        "index_0": {
            "red": red,
            "green": green,
            "blue": blue,
            "color": color_565(red, green, blue),
        },
    }
    missing = data["pictures"][3]
    assert missing["status"] == "missing" and missing["index_0"] is None


def test_schema_file_matches_model():
    text = json.dumps(pictures_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/pictures.schema.json is stale: run python tools/gen_schema.py"
    )


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("pictures", 0, "status"), "gone"),
        (("pictures", 0, "index_0", "red"), 256),
        (("pictures", 0, "picture"), "a.bmp"),
        (("pictures", 0, "name"), "other\\a.bmp"),
        (("transparent_index",), 1),
        (("pictures", 0, "extra"), 0),
    ],
    ids=[
        "status",
        "index-0-red",
        "picture-name",
        "game-name",
        "transparent-index",
        "picture-extra",
    ],
)
def test_schema_rejects_bad_data(tmp_path, validator, path, value):
    view = pictures_view(make_install(tmp_path / "XvT"))
    data = copy.deepcopy(
        json.loads(json.dumps(pictures_to_json(view, png_names(view))))
    )
    target = data
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert list(validator.iter_errors(data)), path


def test_written_png_reads_back(tmp_path):
    root = make_install(tmp_path / "XvT")
    out = tmp_path / "out"
    assert main(["pictures", "export", str(root), str(out)]) == 0
    width, height, pixels = png_pixels((out / "b_x.png").read_bytes())
    assert (width, height) == (3, 2)
    expected = [bytes((*ramp(i), 0 if i == 0 else 255)) for i in (0, 1, 2, 3, 0, 255)]
    assert pixels == expected
    assert (out / "b_x.png").read_bytes() == png_bytes(
        pictures_view(root).pictures[2].bitmap
    )


def test_pictures_export(tmp_path, capsys, validator):
    root = make_install(tmp_path / "XvT")
    out = tmp_path / "out"
    assert (
        main(["pictures", "export", str(root), str(out), "--no-balance-of-power"]) == 0
    )
    assert "exported 4 pictures" in capsys.readouterr().out
    assert sorted(p.name for p in out.glob("*.png")) == [
        "ab.png",
        "a~1.png",
        "b_x.png",
        "shared.png",
    ]
    data = json.loads((out / "pictures.json").read_text(encoding="utf-8"))
    assert data["balance_of_power"] is False
    assert list(validator.iter_errors(data)) == []
    assert main(["pictures", "export", str(tmp_path / "none"), str(out)]) == 2
    blocked = tmp_path / "file"
    blocked.write_bytes(b"")
    assert main(["pictures", "export", str(root), str(blocked)]) == 1


def test_pictures_dump(tmp_path, capsys):
    root = make_install(tmp_path / "XvT")
    path = root / "FrontRes" / "B_X.BMP"
    assert main(["pictures", "dump", str(path)]) == 0
    assert capsys.readouterr().out.startswith(f'picture "{path}" file="{path}" width=3')
    assert main(["pictures", "dump", "frontres\\only.bmp", "--install", str(root)]) == 0
    assert "height=2" in capsys.readouterr().out
    assert main(["pictures", "dump", "frontres\\none.bmp", "--install", str(root)]) == 2
    bad = tmp_path / "bad.bmp"
    bad.write_bytes(b"BM")
    assert main(["pictures", "dump", str(bad)]) == 1

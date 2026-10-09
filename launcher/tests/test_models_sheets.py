"""The models' sheets: an install's list and views, the three renderings, the objects.

Purpose:
    Prove the model list joins both model folders (any letter case),
    lowercases and sorts the names; each view resolves every name
    (missing, loaded from the right folder, or not loaded); the ``file``,
    ``draw`` and ``objects`` renderings print every line as the sheets
    do; the spec lists are cut and counted as the game reads them; and an
    object type names a model only when both flags say so.

Flow:
    Build models with ``optbuilder`` and installs with ``textdata``;
    view, render, compare line by line. The object tables are replaced by
    small synthetic ones where a test needs them.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_models_sheets.py``
"""

from __future__ import annotations

import logging
import struct

import pytest
from optbuilder import build_opt
from optbuilder import descriptor
from optbuilder import Face
from optbuilder import faces
from optbuilder import hardpoint
from optbuilder import N
from optbuilder import palette
from optbuilder import Tex
from optbuilder import uvs
from optbuilder import vecs
from textdata import write_files

from jedimaster.models import CRAFT_OBJECTS
from jedimaster.models import model_lines
from jedimaster.models import model_names
from jedimaster.models import models_view
from jedimaster.models import OBJECT_TYPES
from jedimaster.models import objects as objects_module
from jedimaster.models import objects_view
from jedimaster.models import ObjectType
from jedimaster.models import read_opt
from jedimaster.models import render as render_module
from jedimaster.models import render_draw
from jedimaster.models import render_file
from jedimaster.models import render_objects
from jedimaster.models.game import load_model
from jedimaster.models.objects import craft_model
from jedimaster.models.objects import craft_object
from jedimaster.models.objects import has_model
from jedimaster.models.objects import list_game_path
from jedimaster.models.objects import list_lines
from jedimaster.models.objects import object_model
from jedimaster.models.render import hex64
from jedimaster.models.render import node_lines
from jedimaster.models.render import number
from jedimaster.models.render import quote

logger = logging.getLogger(__name__)


def simple(tag: float = 1.0, version: int = 2) -> bytes:
    """Return a model of one component drawing one triangle."""
    record = Face((0, 1, 2), normal=(tag, 0.0, 0.0))
    group = N(0, children=[
        N(3, count=3, payload=vecs([(0, 0, 0), (1, 0, 0), (0, 1, 0)])),
        N(1, count=1, payload=faces(version, [record], extra=[(0, 0, 1)] * 3)),
    ])  # fmt: skip
    return build_opt([group], version=version)


@pytest.fixture
def install(tmp_path):
    """Return a synthetic install with model files in both folders."""
    return write_files(
        tmp_path,
        {
            "ivfiles/A.OPT": simple(1.0),
            "ivfiles/AB.OPT": simple(5.0),
            "ivfiles/Z-9.opt": simple(2.0),
            "ivfiles/broken.opt": b"\0\0\0\0",
            "ivfiles/readme.txt": b"x",
            "BalanceOfPower/IVFILES/a.opt": simple(3.0, version=2),
            "BalanceOfPower/IVFILES/bop.OPT": simple(4.0),
        },
    )


def test_the_list_joins_both_folders_lowercased_and_sorted(install):
    assert model_names(install) == [
        "ivfiles\\ab.opt",
        "ivfiles\\a.opt",
        "ivfiles\\bop.opt",
        "ivfiles\\broken.opt",
        "ivfiles\\z-9.opt",
    ]
    assert model_names(install / "nowhere") == []


def test_each_view_resolves_every_name(install):
    base = models_view(install, balance_of_power=False)
    base.models = base.models[1:]
    assert [(m.status, m.file) for m in base.models] == [
        ("loaded", "ivfiles/a.opt"),
        ("missing", None),
        ("not_loaded", "ivfiles/broken.opt"),
        ("loaded", "ivfiles/z-9.opt"),
    ]
    bop = models_view(install)
    assert bop.models[1].file == "BalanceOfPower/ivfiles/a.opt"
    assert bop.models[1].model.version == 2 and bop.models[2].status == "loaded"
    text = render_file(base).splitlines()
    assert (
        text[0] == 'model "ivfiles\\\\a.opt" file="ivfiles/a.opt" '
        "version=2 roots=1 nodes=3 reserved=7"
    )
    assert 'model "ivfiles\\\\bop.opt" missing' in text
    assert 'model "ivfiles\\\\broken.opt" file="ivfiles/broken.opt" not_loaded' in text
    assert render_draw(base).count("\nmodel ") == 3


def test_numbers_and_quotes_as_the_sheets_print_them():
    assert [number(v) for v in (-0.0, 1e-05, 0.5, 32767.0, 1e20)] == [
        "-0", "1e-05", "0.5", "32767", "1e+20",
    ]  # fmt: skip
    as_float = struct.unpack("<f", struct.pack("<f", 6.8e-05))[0]
    assert number(as_float) == "6.80000012e-05"
    nan = struct.unpack("<f", b"\0\0\xc0\x7f")[0]
    minus_nan = struct.unpack("<f", b"\0\0\xc0\xff")[0]
    assert (number(nan), number(minus_nan), number(float("-inf"))) == (
        "nan",
        "-nan",
        "-inf",
    )
    assert quote(b'a"b\\c\x01\x7f\xff ') == '"a\\"b\\\\c\\x01\\x7f\\xff "'
    assert quote("ivfiles\\x.opt") == '"ivfiles\\\\x.opt"'


TEXELS = b"\1\2"


def every_type() -> bytes:
    """Return a model holding one node of every type with a payload line."""
    tex = N(
        20,
        b"T",
        payload=Tex(2, 1, TEXELS, texture_size=5, inline=2, palette=palette(2)),
    )
    floats12 = struct.pack("<12f", *[k / 2 for k in range(12)])
    record = Face((0, 1, 2, 3), edges=(1, 2, 3, 4))
    roots = [
        N(0, b"G", children=[None]),
        N(1, count=1, payload=faces(1, [record], edges=4)),
        N(2, payload=floats12),
        N(3, count=1, payload=vecs([(1.5, -2, 0)])),
        N(4, payload=struct.pack("<3f", 1, 2, 3)),
        N(5, payload=struct.pack("<9f", *range(9))),
        N(6, payload=struct.pack("<3f", 4, 5, 6)),
        N(7, payload=b'Na"me\0'),
        N(8),
        N(9, count=1, payload=bytes(range(56))),
        N(11, count=1, payload=vecs([(0, 0, 1)])),
        N(13, count=2, payload=uvs([(0.25, 1), (0, 0)])),
        N(19, payload=struct.pack("<3f", 0.5, 0.25, 1)),
        tex,
        N(21, count=3, payload=struct.pack("<3f", 0.001, 1e-05, 0)),
        N(22, payload=hardpoint(5, (1, -2, 3.5))),
        N(23, payload=floats12),
        N(24, count=4),
        N(25, payload=descriptor(3, 6, 2, seed=0.5)),
        N(-1, count=9),
    ]
    return build_opt(roots, reserved=3)


def test_every_line_of_the_file_sheet(tmp_path):
    path = tmp_path / "x.opt"
    path.write_bytes(every_type())
    lines = model_lines(load_model("ivfiles\\x.opt", path, "ivfiles/x.opt"))
    record = struct.pack("<16i", 0, 1, 2, 3, 1, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0)
    normal = struct.pack("<3f", 0, 0, 1)
    gradients = struct.pack("<6f", *(0.5,) * 6)
    assert (
        lines[0] == 'model "ivfiles\\\\x.opt" file="ivfiles/x.opt" '
        "version=2 roots=20 nodes=20 reserved=3"
    )
    assert lines[1] == "root 0 node=0" and lines[20] == "root 19 node=19"
    body = lines[21:]
    assert body[0] == 'node 0 type=0 name="G" children=- payload_count=1'
    assert body[1:3] == [
        "node 1 type=1 name=- children= payload_count=1",
        f"  faces edges=4 records={hex64(record)} normals={hex64(normal)} "
        f"gradients={hex64(gradients)} extra_normals=0 extra=cbf29ce484222325",
    ]
    assert body[4] == "  values v=0,0.5,1,1.5,2,2.5,3,3.5,4,4.5,5,5.5"
    assert body[6] == f"  vectors hash={hex64(vecs([(1.5, -2, 0)]))}"
    assert body[8] == "  values v=1,2,3" and body[10] == "  values v=0,1,2,3,4,5,6,7,8"
    assert body[12] == "  values v=4,5,6"
    assert body[13:15] == [
        "node 7 type=7 name=- children= payload_count=0",
        '  ref "Na\\"me"',
    ]
    assert body[15] == "node 8 type=8 name=- children= payload_count=1"
    assert body[17] == f"  materials hash={hex64(bytes(range(56)))}"
    assert body[19] == f"  vectors hash={hex64(vecs([(0, 0, 1)]))}"
    assert body[21] == f"  coords hash={hex64(uvs([(0.25, 1), (0, 0)]))}"
    assert body[23] == "  values v=0.5,0.25,1"
    assert body[25] == (
        "  texture width=2 height=1 texture_size=5 data_size=2 inline_palettes=2 "
        f"top={hex64(TEXELS)} texels={hex64(TEXELS)} palette=inline "
        f"block={hex64(palette(2))}"
    )
    assert body[27] == "  levels distances=0.00100000005,9.99999975e-06,0"
    assert body[29] == "  hardpoint type=5 at=1,-2,3.5"
    assert body[31] == "  values v=0,0.5,1,1.5,2,2.5,3,3.5,4,4.5,5,5.5"
    assert body[32] == "node 17 type=24 name=- children= payload_count=4"
    assert body[34] == (
        "  descriptor mesh_type=3 flags=6 span=0.5,1.5,2.5 center=3.5,4.5,5.5 "
        "min=6.5,7.5,8.5 max=9.5,10.5,11.5 target_id=2 target=20.5,21.5,22.5"
    )
    assert body[35:] == ["node 19 type=-1 name=- children= payload_count=9"]


def test_the_draw_sheet_prints_passes_components_and_sorted_faces(install):
    view = models_view(install, balance_of_power=False)
    view.models = [view.models[1], view.models[2]]
    two = Face((0, 1, 2), normal=(2.0, 0.0, 0.0))
    one = Face((0, 1, 2), normal=(1.0, 0.0, 0.0))
    group = N(0, children=[
        N(3, count=3, payload=vecs([(0, 0, 0), (1, 0, 0), (0, 1, 0)])),
        N(1, count=2, payload=faces(1, [two, one], extra=[(0, 0, 1)] * 3)),
    ])  # fmt: skip
    switch = N(24, children=[N(0), N(0)])
    view.models[0].model = read_opt(build_opt([group, switch]))
    lines = render_draw(view).splitlines()
    assert lines[0] == (
        'model "ivfiles\\\\a.opt" file="ivfiles/a.opt" version=2 roots=2 '
        "components=2 lods=1 switches=2"
    )
    assert lines[1] == "pass lod=1 switch=0 meshes=1 faces=2 walk=unchecked"
    assert lines[2] == "component 0 faces=2"
    assert lines[3].endswith("fn=1,0,0") and lines[4].endswith("fn=2,0,0")
    assert lines[3].startswith(
        "face tex=white corners=3 p=0,0,0;1,0,0;0,1,0 uv=bad;bad;bad n="
    )
    assert lines[5] == "component 1 faces=0"
    assert lines[6] == "pass lod=1 switch=1 meshes=1 faces=2 walk=unchecked"
    assert lines[10:] == ["component 1 faces=0", 'model "ivfiles\\\\bop.opt" missing']


def test_list_lines_are_cut_and_counted_as_the_game_reads_them():
    data = b"A.OPT\r\n\nB\0junk\nC\rD\n\r\n\x1a"
    assert list_lines(data) == [b"A.OPT", b"B", b"C", b"\x1a"]
    assert list_lines(b"") == [] and list_lines(b"last") == [b"last"]
    assert list_game_path(0, 640) == "ivfiles\\SPEC640.LST"
    assert list_game_path(2, 320) == "ivfiles\\SPEC3320.LST"


def test_the_flags_that_name_a_model():
    def record(flags: int, assets: int) -> ObjectType:
        return ObjectType(flags, assets, 0, 0, 0)

    assert has_model(record(0x02, 0x01)) and has_model(record(0x03, 0x41))
    assert not has_model(record(0x01, 0x01)) and not has_model(record(0x02, 0x02))
    assert not has_model(record(0x00, 0x00))


TABLE = (
    ObjectType(0x03, 0x01, 0, 0, 1),
    ObjectType(0x03, 0x02, 1, 0, 0),
    ObjectType(0x03, 0x41, 2, 2, 0),
    ObjectType(0x02, 0x01, 3, 0, 5),
    ObjectType(0x03, 0x01, 4, 1, 0),
    ObjectType(0x03, 0x01, 5, 0, 0),
)
CRAFT = (0, 2, 9, 3)


@pytest.fixture
def tables(monkeypatch):
    """Replace the object tables with small synthetic ones everywhere."""
    for module in (objects_module, render_module):
        monkeypatch.setattr(module, "OBJECT_TYPES", TABLE)
        monkeypatch.setattr(module, "CRAFT_OBJECTS", CRAFT)


@pytest.fixture
def lists_install(tmp_path):
    """Return an install with two spec lists, one replaced by Balance of Power."""
    return write_files(
        tmp_path,
        {
            "ivfiles/SPEC640.LST": b"ivfiles\\ONE.OPT\r\nivfiles\\TWO.OPT\r\n",
            "ivfiles/spec320.lst": b"ivfiles\\small.opt\n",
            "BalanceOfPower/IVFILES/spec3640.lst": b"\nivfiles\\Bop.opt\n\x1a",
        },
    )


def test_object_and_craft_models(tables, lists_install):
    view = objects_view(lists_install)
    assert [object_model(view, t) for t in range(-1, 7)] == [
        None, "ivfiles\\TWO.OPT", None, "ivfiles\\Bop.opt", None, None,
        "ivfiles\\ONE.OPT", None,
    ]  # fmt: skip
    assert object_model(view, 0, 320) is None
    assert object_model(objects_view(lists_install, False), 2) is None
    assert [craft_object(c) for c in (-1, 0, 2, 4)] == [None, 0, 9, None]
    assert [craft_model(view, c) for c in range(5)] == [
        "ivfiles\\TWO.OPT", "ivfiles\\Bop.opt", None, None, None,
    ]  # fmt: skip


def test_the_objects_sheet(tables, lists_install):
    lines = render_objects(objects_view(lists_install)).splitlines()
    assert lines[:2] == [
        "object 0 record_flags=0x03 asset_flags=0x01 model_index=0 "
        "texture_group=0 resource_index=1",
        "object 1 record_flags=0x03 asset_flags=0x02 model_index=1 "
        "texture_group=0 resource_index=0",
    ]
    assert lines[6:10] == [
        "craft 0 object=0",
        "craft 1 object=2",
        "craft 2 object=9",
        "craft 3 object=3",
    ]
    assert lines[10:] == [
        'list 0 640 "ivfiles\\\\SPEC640.LST" file="ivfiles/SPEC640.LST"',
        'entry 0 640 0 "ivfiles\\\\ONE.OPT"',
        'entry 0 640 1 "ivfiles\\\\TWO.OPT"',
        'list 0 320 "ivfiles\\\\SPEC320.LST" file="ivfiles/SPEC320.LST"',
        'entry 0 320 0 "ivfiles\\\\small.opt"',
        'list 1 640 "ivfiles\\\\SPEC2640.LST" missing',
        'list 1 320 "ivfiles\\\\SPEC2320.LST" missing',
        'list 2 640 "ivfiles\\\\SPEC3640.LST" '
        'file="BalanceOfPower/ivfiles/SPEC3640.LST"',
        'entry 2 640 0 "ivfiles\\\\Bop.opt"',
        'entry 2 640 1 "\\x1a"',
        'list 2 320 "ivfiles\\\\SPEC3320.LST" missing',
    ]


def test_the_tables_cover_every_type():
    assert len(OBJECT_TYPES) == 201 and len(CRAFT_OBJECTS) == 96
    for record in OBJECT_TYPES:
        assert all(0 <= value <= 255 for value in record)
        assert record.texture_group in (0, 1, 2)


def test_the_faces_line_counts_extra_normals():
    extra = [(0.0, 0.0, 1.0), (1.0, 0.0, 0.0)]
    group = N(0, children=[
        N(3, count=2, payload=vecs([(0, 0, 0), (1, 0, 0)])),
        N(1, count=1, payload=faces(1, [Face((0, 1, 1))], edges=2, extra=extra)),
    ])  # fmt: skip
    model = read_opt(build_opt([group]))
    line = node_lines(model.nodes[2], model.version)[1]
    assert line.endswith(f" extra_normals=2 extra={hex64(vecs(extra))}")
    assert line.startswith("  faces edges=2 records=")

"""The glTF export: a written .glb read back, byte by byte.

Purpose:
    Prove a ``.glb`` has the GLB header and its two chunks, one node per
    model with one per component and its hardpoints, accessors whose
    ``min`` and ``max`` hold, positions and normals turned into glTF's
    frame, triangles wound toward the turned face normal, unit normals,
    one material per texture (white, glowing, glowing taken as the last
    texture, ``_`` named, unnamed), and PNG pictures that inflate to the
    texture's colors.

Flow:
    Build a synthetic model with ``optbuilder``; write its ``.glb``; read
    the header, the chunks, the JSON and the arrays back; check them.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_models_gltf.py``
"""

from __future__ import annotations

import json
import logging
import math
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
from test_fonts_output import png_pixels

from jedimaster.models import glb_bytes
from jedimaster.models import glb_names
from jedimaster.models import read_opt
from jedimaster.models import rgb8
from jedimaster.models.gltf import cross
from jedimaster.models.gltf import glows
from jedimaster.models.gltf import texture_name
from jedimaster.models.gltf import turn
from jedimaster.models.gltf import unit
from jedimaster.models.gltf import wind

logger = logging.getLogger(__name__)

BRIGHT = (20 << 11) | (20 << 6) | 20
GREEN = 0x07E0


def glow_color(sub: int, index: int) -> int:
    """Return a glowing palette's color: index 0 dark, the rest steady."""
    if index == 0:
        return 0
    return GREEN if sub == 10 else BRIGHT


def plain_color(sub: int, index: int) -> int:
    """Return a palette's color in which no index glows; sub-palette 8 varies."""
    if sub == 8:
        return (index << 5) | 0x1F
    return 0xFFFF if sub == 0 else 0


POINTS = [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (10.0, 20.0, 0.0), (0.0, 20.0, 0.0)]


def tri(normal: tuple[float, float, float], tag: int = 0, normals=(2, 2, 2)) -> N:
    """Return one triangle 0, 1, 2 with ``normal`` as its face normal."""
    record = Face((0, 1, 2), coords=(0, 1, 2), normals=normals, normal=normal)
    return N(1, count=1, payload=faces(1, [record], edges=3 + tag))


def mesh_lists() -> list[N]:
    """Return vertices, coordinates and normals (non-unit, zero, slanted)."""
    return [
        N(3, count=4, payload=vecs(POINTS)),
        N(13, count=4, payload=uvs([(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])),
        N(
            11,
            count=3,
            payload=vecs([(0.0, 0.0, 2.0), (0.0, 0.0, 0.0), (3.0, 4.0, 0.0)]),
        ),
    ]


def ship() -> bytes:
    """Return a model of three components exercising every export rule."""
    glow_tex = N(
        20, b"Glow", payload=Tex(2, 2, b"\0\1\2\3", palette=palette(color=glow_color))
    )
    dark = N(20, b"_Dark", payload=Tex(1, 1, b"\1", shares=glow_tex))
    plain = N(20, None, payload=Tex(2, 1, b"\5\6", palette=palette(color=plain_color)))
    quad = Face(
        (0, 1, 2, 3), coords=(0, 1, 2, 3), normals=(0, 1, 2, 0), normal=(0, 0, 3.0)
    )
    quad_node = N(1, count=1, payload=faces(1, [quad], edges=4))
    mesh = N(0, children=[
        *mesh_lists(), tri((0, 0, -2.0), normals=(1, 1, 0)), dark, tri((0, 0, -1.0), 1),
        N(0, children=[plain, tri((0, 0, -1.0), 2)]), glow_tex, quad_node,
    ])  # fmt: skip
    first = N(0, children=[
        N(25, payload=descriptor(6, 2, 9)), hardpoint_node(4, (1.0, 2.0, 3.0)),
        hardpoint_node(7, (-5.0, 6.0, 0.5)), mesh,
    ])  # fmt: skip
    second = N(0, children=[*mesh_lists(), tri((0, 0, -1.0), 3)])
    return build_opt([first, second, N(0)])


def hardpoint_node(kind: int, at: tuple[float, float, float]) -> N:
    """Return a hardpoint node."""
    return N(22, payload=hardpoint(kind, at))


def read_glb(data: bytes) -> tuple[dict, bytes]:
    """Return a GLB's JSON and binary chunk, checking its header and padding."""
    magic, version, length = struct.unpack_from("<III", data)
    assert (magic, version, length) == (0x46546C67, 2, len(data))
    json_length, json_kind = struct.unpack_from("<II", data, 12)
    assert json_kind == 0x4E4F534A and json_length % 4 == 0
    text = data[20 : 20 + json_length]
    document = json.loads(text)
    assert len(text) - len(text.rstrip(b" ")) < 4 and text.rstrip(b" ").endswith(b"}")
    at = 20 + json_length
    if at == len(data):
        return document, b""
    bin_length, bin_kind = struct.unpack_from("<II", data, at)
    assert bin_kind == 0x004E4942 and bin_length % 4 == 0
    assert at + 8 + bin_length == len(data)
    binary = data[at + 8 :]
    assert 0 <= len(binary) - document["buffers"][0]["byteLength"] < 4
    return document, binary


def accessor(document: dict, binary: bytes, index: int) -> list[tuple]:
    """Return an accessor's rows, read from its buffer view."""
    item = document["accessors"][index]
    view = document["bufferViews"][item["bufferView"]]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}[item["type"]]
    code = {5126: "f", 5125: "I"}[item["componentType"]]
    start = view["byteOffset"]
    assert start % 4 == 0 and view["byteLength"] == 4 * width * item["count"]
    values = struct.unpack_from(f"<{width * item['count']}{code}", binary, start)
    return [tuple(values[k * width : (k + 1) * width]) for k in range(item["count"])]


@pytest.fixture(scope="module")
def exported():
    """Return the model and its switch 0 file's JSON and binary chunk."""
    model = read_opt(ship())
    document, binary = read_glb(glb_bytes(model, "ship"))
    return model, document, binary


def primitives(document: dict, component: int) -> list[dict]:
    """Return the primitives of component ``component``'s mesh."""
    node = document["nodes"][document["nodes"][0]["children"][component]]
    return document["meshes"][node["mesh"]]["primitives"]


def material_names(document: dict, component: int) -> list[str]:
    """Return the material name of each primitive of a component, in order."""
    return [
        document["materials"][p["material"]]["name"]
        for p in primitives(document, component)
    ]


def test_nodes_hardpoints_and_extras(exported):
    _, document, _ = exported
    nodes = document["nodes"]
    assert document["scenes"] == [{"name": "ship", "nodes": [0]}]
    assert nodes[0]["name"] == "ship"
    components = [nodes[i] for i in nodes[0]["children"]]
    assert [c["name"] for c in components] == [
        "component 0",
        "component 1",
        "component 2",
    ]
    assert components[0]["extras"] == {"mesh_type": 6, "flags": 2, "target_id": 9}
    hardpoints = [nodes[i] for i in components[0]["children"]]
    assert hardpoints == [
        {"name": "hardpoint 4", "translation": [-1.0, 3.0, -2.0]},
        {"name": "hardpoint 7", "translation": [5.0, 0.5, -6.0]},
    ]
    assert "mesh" not in components[2] and "extras" not in components[1]
    assert document["asset"]["version"] == "2.0"


def test_one_material_per_texture_and_the_glow_rule(exported):
    model, document, _ = exported
    assert material_names(document, 0) == ["white", "_Dark", "texture 12", "Glow"]
    assert material_names(document, 1) == ["Glow without glow"]
    materials = {m["name"]: m for m in document["materials"]}
    assert materials["white"]["pbrMetallicRoughness"] == {
        "metallicFactor": 0,
        "roughnessFactor": 1,
        "baseColorFactor": [1, 1, 1, 1],
    }
    glow = materials["Glow"]
    assert glow["emissiveFactor"] == [1, 1, 1] and "emissiveTexture" in glow
    for name in ("_Dark", "texture 12", "Glow without glow"):
        assert (
            "emissiveTexture" not in materials[name]
            and "emissiveFactor" not in materials[name]
        )
        pbr = materials[name]["pbrMetallicRoughness"]
        assert (pbr["metallicFactor"], pbr["roughnessFactor"]) == (0, 1)
    assert texture_name(model, 12) == "texture 12" and texture_name(model, 9) == "_Dark"
    assert [glows(model, n) for n in (9, 12, 14, 8)] == [False, False, True, False]


def test_pictures_inflate_to_the_texture_colors(exported):
    _, document, binary = exported
    images = document["images"]
    assert all(i["mimeType"] == "image/png" for i in images)

    def pixels(name: str) -> list[bytes]:
        (image,) = [i for i in images if i["name"] == name]
        view = document["bufferViews"][image["bufferView"]]
        png = binary[view["byteOffset"] : view["byteOffset"] + view["byteLength"]]
        return png_pixels(png)[2]

    black, bright, green = (
        b"\0\0\0\xff",
        bytes((*rgb8(BRIGHT), 255)),
        bytes((*rgb8(GREEN), 255)),
    )
    assert pixels("Glow") == [black, bright, bright, bright]
    assert pixels("Glow glow") == [black, green, green, green]
    assert pixels("texture 12") == [
        bytes((*rgb8((k << 5) | 0x1F), 255)) for k in (5, 6)
    ]
    assert pixels("_Dark") == [bright]
    textures = document["textures"]
    materials = {m["name"]: m for m in document["materials"]}
    glow = materials["Glow"]
    source = textures[glow["emissiveTexture"]["index"]]["source"]
    assert images[source]["name"] == "Glow glow"
    unlit = materials["Glow without glow"]["pbrMetallicRoughness"]["baseColorTexture"]
    assert unlit == glow["pbrMetallicRoughness"]["baseColorTexture"]
    assert len(images) == len(textures) == 4


def test_positions_are_turned_with_their_bounds(exported):
    _, document, binary = exported
    (quad,) = [
        p
        for p in primitives(document, 0)
        if document["materials"][p["material"]]["name"] == "Glow"
    ]
    positions = accessor(document, binary, quad["attributes"]["POSITION"])
    assert positions == [turn(p) for p in POINTS]
    item = document["accessors"][quad["attributes"]["POSITION"]]
    assert item["min"] == [-10.0, 0.0, -20.0] and item["max"] == [0.0, 0.0, 0.0]
    uv = accessor(document, binary, quad["attributes"]["TEXCOORD_0"])
    assert uv == [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]


FACE_NORMALS = {"Glow": (0.0, 1.0, 0.0)}
"""The turned face normal of each material's faces; the others face (0, -1, 0)."""


def test_triangles_wind_toward_the_turned_face_normal(exported):
    _, document, binary = exported
    for component in (0, 1):
        for primitive in primitives(document, component):
            name = document["materials"][primitive["material"]]["name"]
            outside = FACE_NORMALS.get(name, (0.0, -1.0, 0.0))
            positions = accessor(document, binary, primitive["attributes"]["POSITION"])
            flat = [i for (i,) in accessor(document, binary, primitive["indices"])]
            assert primitive["mode"] == 4 and len(flat) % 3 == 0 and flat
            for k in range(0, len(flat), 3):
                a, b, c = (positions[i] for i in flat[k : k + 3])
                ab = tuple(y - x for x, y in zip(a, b, strict=True))
                ac = tuple(y - x for x, y in zip(a, c, strict=True))
                facing = cross(ab, ac)
                assert sum(f * n for f, n in zip(facing, outside, strict=True)) > 0
    (quad,) = [
        p
        for p in primitives(document, 0)
        if document["materials"][p["material"]]["name"] == "Glow"
    ]
    assert [i for (i,) in accessor(document, binary, quad["indices"])] == [
        0,
        2,
        1,
        0,
        3,
        2,
    ]
    (white,) = [
        p
        for p in primitives(document, 0)
        if document["materials"][p["material"]]["name"] == "white"
    ]
    assert [i for (i,) in accessor(document, binary, white["indices"])] == [0, 1, 2]


def test_normals_are_unit_length_and_zero_ones_replaced(exported):
    _, document, binary = exported
    (quad,) = [
        p
        for p in primitives(document, 0)
        if document["materials"][p["material"]]["name"] == "Glow"
    ]
    normals = accessor(document, binary, quad["attributes"]["NORMAL"])
    assert normals[0] == (0.0, 1.0, 0.0) and normals[3] == (0.0, 1.0, 0.0)
    assert normals[1] == (0.0, 1.0, 0.0)
    assert normals[2] == pytest.approx((-0.6, 0.0, -0.8))
    (white,) = [
        p
        for p in primitives(document, 0)
        if document["materials"][p["material"]]["name"] == "white"
    ]
    rows = accessor(document, binary, white["attributes"]["NORMAL"])
    assert rows == [(0.0, -1.0, 0.0), (0.0, -1.0, 0.0), (0.0, 1.0, 0.0)]
    for primitive in primitives(document, 0):
        for row in accessor(document, binary, primitive["attributes"]["NORMAL"]):
            assert math.isclose(math.sqrt(sum(v * v for v in row)), 1.0, rel_tol=1e-6)


def test_the_vector_helpers():
    assert turn((1.0, 2.0, 3.0)) == (-1.0, 3.0, -2.0)
    assert unit((0.0, 3.0, 4.0), (9.0, 9.0, 9.0)) == (0.0, 0.6, 0.8)
    assert unit((0.0, 0.0, 0.0), (1.0, 0.0, 0.0)) == (1.0, 0.0, 0.0)
    assert unit(None, (0.0, 1.0, 0.0)) == (0.0, 1.0, 0.0)
    flat = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)]
    assert wind(flat, (0.0, 0.0, 1.0)) == [0, 1, 2]
    assert wind(flat, (0.0, 0.0, -1.0)) == [0, 2, 1]
    assert wind(flat, (1.0, 0.0, 0.0)) == [0, 1, 2]


def test_switch_files_and_a_model_with_no_faces():
    assert glb_names("xwing", 1) == ["xwing.glb"]
    assert glb_names("z-95", 3) == ["z-95.glb", "z-95_s1.glb", "z-95_s2.glb"]
    bare = read_opt(build_opt([N(0)]))
    document, binary = read_glb(glb_bytes(bare, "bare"))
    assert binary == b"" and "buffers" not in document and "meshes" not in document
    switch = N(24, children=[N(0), N(0, children=[*mesh_lists(), tri((0, 0, 1.0))])])
    model = read_opt(build_opt([N(0, children=[switch])]))
    assert "meshes" not in read_glb(glb_bytes(model, "s", 0))[0]
    assert len(read_glb(glb_bytes(model, "s", 1))[0]["meshes"]) == 1


def test_the_export_draws_detail_level_1():
    far = Face((0, 1, 3), normal=(0, 0, 1.0))
    near = Face((0, 1, 2, 3), normal=(0, 0, 1.0))
    levels = N(21, count=2, payload=b"\0" * 8, children=[
        N(1, count=1, payload=faces(1, [near])), N(1, count=1, payload=faces(1, [far])),
    ])  # fmt: skip
    model = read_opt(build_opt([N(0, children=[*mesh_lists(), tex_root(), levels])]))
    document, binary = read_glb(glb_bytes(model, "lod"))
    (primitive,) = primitives(document, 0)
    assert len(accessor(document, binary, primitive["attributes"]["POSITION"])) == 4
    (image,) = document["images"]
    assert document["bufferViews"][image["bufferView"]]["byteLength"] % 4 != 0


def test_bad_values_in_the_export():
    bad_position = Face((0, 1, 9), normal=(0, 0, 1.0))
    bad_uv = Face((0, 1, 2), coords=(0, 7, 2), normals=(0, 9, 0), normal=(0, 0, 1.0))
    node = N(1, count=2, payload=faces(1, [bad_position, bad_uv]))
    lone = N(1, count=1, payload=faces(1, [bad_position]))
    model = read_opt(build_opt([
        N(0, children=[*mesh_lists(), node]),
        N(0, children=[*mesh_lists(), tex_root(), lone]),
    ]))  # fmt: skip
    document, binary = read_glb(glb_bytes(model, "bad"))
    (primitive,) = primitives(document, 0)
    uv = accessor(document, binary, primitive["attributes"]["TEXCOORD_0"])
    assert uv == [(0.0, 0.0), (0.0, 0.0), (1.0, 1.0)]
    normals = accessor(document, binary, primitive["attributes"]["NORMAL"])
    assert normals[1] == (0.0, 1.0, 0.0)
    assert [m["name"] for m in document["materials"]] == ["white"]
    assert "mesh" not in document["nodes"][document["nodes"][0]["children"][1]]


def tex_root() -> N:
    """Return a named texture node with its own plain palette."""
    return N(20, b"Plain", payload=Tex(1, 1, b"\0", palette=palette(color=plain_color)))

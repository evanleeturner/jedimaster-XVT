"""Write what a pass draws as one binary glTF 2.0 file (.glb).

Purpose:
    Give a page a model the way the game draws it at one detail level and
    switch: one node per model with one child per component, each with a
    mesh of one primitive per material it draws, its hardpoints as child
    nodes and its mesh descriptor's numbers in ``extras``; the textures as
    PNG pictures inside the file.

Flow:
    ``glb_bytes`` draws the pass (``draw.draw_pass``), turns each face
    into triangles (``_Primitive.add``), builds materials and pictures
    (``_Materials``), packs every array into one buffer (``_Buffer``) and
    wraps the JSON and the buffer in the GLB header and chunks.

Invariants:
    - The model's frame (+x right, -y front, +z top, left-handed) turns
      into glTF's (+Y up, front toward +Z, right-handed): ``(x, y, z)``
      becomes ``(-x, z, -y)``; sizes as stored.
    - A four-corner face is the triangles 0, 1, 2 and 0, 2, 3; each
      triangle's corners are ordered so that, after the turn, its
      counter-clockwise normal points the way the turned face normal does
      (glTF's front side is the face's outside); a triangle with no area
      or a face normal of 0 keeps its order. Materials are one-sided.
    - Normals are unit length: a stored normal scaled to length 1, a zero
      or bad one replaced by the turned face normal at unit length (``+Y``
      when that is 0 too). A face with a bad position is left out; a bad
      texture coordinate becomes ``0, 0``.
    - Base colors are the top level through sub-palette 8, opaque. A
      texture that glows and whose name does not start with ``_`` also
      gets an emissive picture (glow colors, black where an index does not
      glow) with ``emissiveFactor`` ``[1, 1, 1]``; faces that drew it as
      the last texture use a second material of it without the glow. The
      white texture is a white ``baseColorFactor`` with no picture.

Call:
    ``data = glb_bytes(model, "xwing", switch=0)``
"""

from __future__ import annotations

import json
import logging
import math
import struct
from dataclasses import dataclass
from dataclasses import field
from typing import Any

from ..icons.png import rgba_png
from .colors import base_sub_palette
from .colors import glow_of
from .colors import rgb8
from .draw import Corner
from .draw import draw_pass
from .draw import DrawnFace
from .draw import TEXTURE_ROOTS
from .model import HARDPOINT
from .model import MESH_DESCRIPTOR
from .model import OptModel
from .model import TEXTURE

logger = logging.getLogger(__name__)

GLB_MAGIC = 0x46546C67
GLB_VERSION = 2
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942
FLOAT = 5126
UNSIGNED_INT = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963
TRIANGLES = 4
EXPORT_LOD = 1
UP = (0.0, 1.0, 0.0)

Vector = tuple[float, float, float]


def turn(vector: Vector) -> Vector:
    """Return a model-frame vector in glTF's frame: ``(-x, z, -y)``.

    Does not scale or check the vector.
    """
    x, y, z = vector
    return (-x, z, -y)


def _sub(a: Vector, b: Vector) -> Vector:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a: Vector, b: Vector) -> Vector:
    """Return the cross product ``a x b``; does not normalise it."""
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a: Vector, b: Vector) -> float:
    """Return the dot product of two vectors; does not normalise them."""
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def unit(vector: Vector | None, fallback: Vector) -> Vector:
    """Return ``vector`` scaled to length 1, or ``fallback`` for none or zero.

    Does not scale ``fallback``.
    """
    if vector is None:
        return fallback
    length = math.sqrt(dot(vector, vector))
    if length == 0.0 or not math.isfinite(length):
        return fallback
    return (vector[0] / length, vector[1] / length, vector[2] / length)


def wind(corners: list[Vector], normal: Vector) -> list[int]:
    """Return the order 0, 1, 2 or 0, 2, 1 that turns a triangle toward ``normal``.

    The order whose counter-clockwise normal ``(b - a) x (c - a)`` has a
    positive dot product with ``normal``; 0, 1, 2 when the dot product is
    0 (no area, or no normal). Does not turn anything.
    """
    a, b, c = corners
    facing = dot(cross(_sub(b, a), _sub(c, a)), normal)
    return [0, 2, 1] if facing < 0 else [0, 1, 2]


@dataclass
class _Primitive:
    """One primitive's arrays, built face by face."""

    material: int
    positions: list[Vector] = field(default_factory=list)
    normals: list[Vector] = field(default_factory=list)
    uvs: list[tuple[float, float]] = field(default_factory=list)
    indices: list[int] = field(default_factory=list)

    def add(self, face: DrawnFace) -> bool:
        """Add a face's corners and triangles; return False for a bad position."""
        if any(c.position is None for c in face.corners):
            return False
        face_normal = unit(turn(face.normal), UP)
        start = len(self.positions)
        for corner in face.corners:
            self._corner(corner.position or (0.0, 0.0, 0.0), corner, face_normal)
        triangles = [[0, 1, 2]] + ([[0, 2, 3]] if len(face.corners) == 4 else [])
        for triangle in triangles:
            points = [self.positions[start + k] for k in triangle]
            order = wind(points, face_normal)
            self.indices += [start + triangle[k] for k in order]
        return True

    def _corner(self, position: Vector, corner: Corner, face_normal: Vector) -> None:
        self.positions.append(turn(position))
        normal = None if corner.normal is None else turn(corner.normal)
        self.normals.append(unit(normal, face_normal))
        self.uvs.append(corner.uv if corner.uv is not None else (0.0, 0.0))


@dataclass
class _Buffer:
    """The binary chunk: every array, each in its own 4-byte aligned view."""

    data: bytearray = field(default_factory=bytearray)
    views: list[dict[str, Any]] = field(default_factory=list)
    accessors: list[dict[str, Any]] = field(default_factory=list)

    def view(self, blob: bytes, target: int | None = None) -> int:
        """Append ``blob`` as a buffer view; return the view's index."""
        while len(self.data) % 4:
            self.data.append(0)
        view: dict[str, Any] = {
            "buffer": 0,
            "byteOffset": len(self.data),
            "byteLength": len(blob),
        }
        if target is not None:
            view["target"] = target
        self.data += blob
        self.views.append(view)
        return len(self.views) - 1

    def floats(self, rows: list[tuple[float, ...]], kind: str, bounds: bool) -> int:
        """Append float rows as an accessor; return the accessor's index."""
        width = len(rows[0])
        blob = struct.pack(f"<{width * len(rows)}f", *(v for row in rows for v in row))
        accessor: dict[str, Any] = {
            "bufferView": self.view(blob, ARRAY_BUFFER),
            "componentType": FLOAT,
            "count": len(rows),
            "type": kind,
        }
        if bounds:
            stored = [struct.unpack("<3f", struct.pack("<3f", *row)) for row in rows]
            accessor["min"] = [min(row[k] for row in stored) for k in range(width)]
            accessor["max"] = [max(row[k] for row in stored) for k in range(width)]
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def indices(self, values: list[int]) -> int:
        """Append triangle indices as an accessor; return the accessor's index."""
        blob = struct.pack(f"<{len(values)}I", *values)
        self.accessors.append(
            {
                "bufferView": self.view(blob, ELEMENT_ARRAY_BUFFER),
                "componentType": UNSIGNED_INT,
                "count": len(values),
                "type": "SCALAR",
            }
        )
        return len(self.accessors) - 1


def texture_name(model: OptModel, number: int) -> str:
    """Return a texture node's name (Latin-1), or ``texture N`` for none.

    Does not check that node ``number`` is a texture.
    """
    name = model.nodes[number].name
    return f"texture {number}" if name is None else name.decode("latin-1")


def glows(model: OptModel, number: int) -> bool:
    """Return True when a texture draws a glow over faces that name it.

    Its palette glows (``colors.glow_of``) and it has a name that does not
    start with ``_``. Does not check that ``number`` is a texture node.
    """
    node = model.nodes[number]
    texture = node.texture
    if texture is None or node.name is None or node.name.startswith(b"_"):
        return False
    return glow_of(texture).glows


def base_png(model: OptModel, number: int) -> bytes:
    """Return a texture's top level through its base sub-palette as a PNG.

    Opaque 8-bit RGBA. Raises ``ValueError`` for a node without a texture.
    Does not apply the glow.
    """
    texture = model.nodes[number].texture
    if texture is None:
        raise ValueError(f"node {number} is not a texture")
    table = [bytes((*rgb8(c), 255)) for c in texture.colors(base_sub_palette(texture))]
    pixels = b"".join(table[i] for i in texture.top)
    return rgba_png(texture.width, texture.height, pixels)


def glow_png(model: OptModel, number: int) -> bytes:
    """Return a texture's glow picture as a PNG: glow colors, black elsewhere.

    Opaque 8-bit RGBA. Raises ``ValueError`` for a node without a texture.
    Does not check that the texture glows.
    """
    texture = model.nodes[number].texture
    if texture is None:
        raise ValueError(f"node {number} is not a texture")
    glow = glow_of(texture)
    table = [
        bytes((*rgb8(color), 255)) if lit else b"\0\0\0\xff"
        for color, lit in zip(glow.colors, glow.mask, strict=True)
    ]
    pixels = b"".join(table[i] for i in texture.top)
    return rgba_png(texture.width, texture.height, pixels)


@dataclass
class _Materials:
    """The materials, textures and pictures of one file, made as first used."""

    model: OptModel
    buffer: _Buffer
    materials: list[dict[str, Any]] = field(default_factory=list)
    textures: list[dict[str, Any]] = field(default_factory=list)
    images: list[dict[str, Any]] = field(default_factory=list)
    by_key: dict[tuple[int | None, bool], int] = field(default_factory=dict)
    pictures: dict[tuple[int, bool], int] = field(default_factory=dict)

    def material(self, face: DrawnFace) -> int:
        """Return the index of the material a drawn face uses, making it once."""
        number = face.texture
        lit = number is not None and face.named and glows(self.model, number)
        key = (number, lit)
        if key not in self.by_key:
            self.by_key[key] = len(self.materials)
            self.materials.append(self._make(number, lit))
        return self.by_key[key]

    def _picture(self, number: int, glow: bool) -> int:
        """Return the glTF texture of a texture node's base or glow picture, once."""
        key = (number, glow)
        if key not in self.pictures:
            name = texture_name(self.model, number) + (" glow" if glow else "")
            png = (glow_png if glow else base_png)(self.model, number)
            view = self.buffer.view(png)
            image = {"name": name, "bufferView": view, "mimeType": "image/png"}
            self.images.append(image)
            self.textures.append({"source": len(self.images) - 1})
            self.pictures[key] = len(self.textures) - 1
        return self.pictures[key]

    def _make(self, number: int | None, lit: bool) -> dict[str, Any]:
        pbr: dict[str, Any] = {"metallicFactor": 0, "roughnessFactor": 1}
        if number is None:
            pbr["baseColorFactor"] = [1, 1, 1, 1]
            return {"name": "white", "pbrMetallicRoughness": pbr}
        name = texture_name(self.model, number)
        pbr["baseColorTexture"] = {"index": self._picture(number, False)}
        if not lit:
            suffix = " without glow" if glows(self.model, number) else ""
            return {"name": name + suffix, "pbrMetallicRoughness": pbr}
        return {
            "name": name,
            "pbrMetallicRoughness": pbr,
            "emissiveTexture": {"index": self._picture(number, True)},
            "emissiveFactor": [1, 1, 1],
        }


def _subtree(model: OptModel, root: int | None, kind: int) -> list[int]:
    """Return the nodes of one type under ``root`` (itself included), in order."""
    found: list[int] = []
    stack = [] if root is None else [root]
    seen: set[int] = set()
    while stack:
        number = stack.pop()
        if number in seen:
            continue
        seen.add(number)
        node = model.nodes[number]
        if node.type == kind:
            found.append(number)
        stack += [c for c in reversed(node.children) if c is not None]
    return found


def component_extras(model: OptModel, root: int | None) -> dict[str, int]:
    """Return a component's mesh descriptor's mesh type, flags and target id.

    From the first mesh descriptor under the component's root; ``{}`` when
    it has none. Does not read the descriptor's floats.
    """
    found = _subtree(model, root, MESH_DESCRIPTOR)
    if not found:
        return {}
    payload = model.nodes[found[0]].payload
    mesh_type, flags = struct.unpack_from("<2i", payload)
    target_id = struct.unpack_from("<i", payload, 56)[0]
    return {"mesh_type": mesh_type, "flags": flags, "target_id": target_id}


def hardpoint_nodes(model: OptModel, root: int | None) -> list[dict[str, Any]]:
    """Return a component's hardpoints as glTF nodes, positions turned.

    Each hardpoint node under the component's root once, in node order,
    named ``hardpoint <type>``. Does not check the type numbers.
    """
    nodes = []
    for number in sorted(_subtree(model, root, HARDPOINT)):
        payload = model.nodes[number].payload
        kind = struct.unpack_from("<i", payload)[0]
        at = turn(struct.unpack_from("<3f", payload, 4))
        nodes.append({"name": f"hardpoint {kind}", "translation": list(at)})
    return nodes


def _mesh(
    component: int, faces: tuple[DrawnFace, ...], materials: _Materials
) -> dict[str, Any] | None:
    """Return one component's glTF mesh, or None when it draws no face."""
    primitives: dict[int, _Primitive] = {}
    for face in faces:
        if any(c.position is None for c in face.corners):
            logger.debug("component %d: face with a bad position left out", component)
            continue
        index = materials.material(face)
        primitives.setdefault(index, _Primitive(index)).add(face)
    buffer = materials.buffer
    out = []
    for primitive in primitives.values():
        attributes = {
            "POSITION": buffer.floats(primitive.positions, "VEC3", True),
            "NORMAL": buffer.floats(primitive.normals, "VEC3", False),
            "TEXCOORD_0": buffer.floats(primitive.uvs, "VEC2", False),
        }
        out.append(
            {
                "attributes": attributes,
                "indices": buffer.indices(primitive.indices),
                "material": primitive.material,
                "mode": TRIANGLES,
            }
        )
    return {"name": f"component {component}", "primitives": out} if out else None


def _component_roots(model: OptModel) -> dict[int, int | None]:
    """Return the root of each component 0 to C - 1."""
    roots: dict[int, int | None] = {}
    for root in model.roots:
        if root is None or model.nodes[root].type != TEXTURE:
            roots[len(roots)] = root
    return roots


def gltf_document(
    model: OptModel, name: str, switch: int = 0
) -> tuple[dict[str, Any], bytes]:
    """Return the glTF JSON and the binary buffer of one pass at detail level 1.

    One scene with one node named ``name``; one child per component
    (``component K``) with its mesh (none when it draws no face), its
    hardpoints and, in ``extras``, its mesh descriptor's numbers. Does
    not check ``name``.
    """
    drawn = draw_pass(model, EXPORT_LOD, switch)
    buffer = _Buffer()
    materials = _Materials(model, buffer)
    nodes: list[dict[str, Any]] = [{"name": name, "children": []}]
    meshes: list[dict[str, Any]] = []
    roots = _component_roots(model)
    for component in drawn.components:
        root = (
            roots.get(component.number) if component.number != TEXTURE_ROOTS else None
        )
        node: dict[str, Any] = {"name": f"component {component.number}"}
        nodes[0]["children"].append(len(nodes))
        nodes.append(node)
        mesh = _mesh(component.number, component.faces, materials)
        if mesh is not None:
            node["mesh"] = len(meshes)
            meshes.append(mesh)
        hardpoints = hardpoint_nodes(model, root)
        if hardpoints:
            node["children"] = list(range(len(nodes), len(nodes) + len(hardpoints)))
            nodes += hardpoints
        extras = component_extras(model, root)
        if extras:
            node["extras"] = extras
    document: dict[str, Any] = {
        "asset": {"version": "2.0", "generator": "jedimaster"},
        "scene": 0,
        "scenes": [{"name": name, "nodes": [0]}],
        "nodes": nodes,
    }
    parts = {
        "meshes": meshes,
        "materials": materials.materials,
        "textures": materials.textures,
        "images": materials.images,
        "accessors": buffer.accessors,
        "bufferViews": buffer.views,
    }
    document |= {key: value for key, value in parts.items() if value}
    if buffer.data:
        document["buffers"] = [{"byteLength": len(buffer.data)}]
    logger.debug("gltf %s switch %d: %d meshes", name, switch, len(meshes))
    return document, bytes(buffer.data)


def glb_bytes(model: OptModel, name: str, switch: int = 0) -> bytes:
    """Return one binary glTF 2.0 file of a model at detail level 1 and ``switch``.

    The 12-byte header, the JSON chunk (padded with spaces to 4 bytes) and,
    when there is any binary data, the binary chunk (padded with zeros).
    Does not write a file.
    """
    document, binary = gltf_document(model, name, switch)
    text = json.dumps(document, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)
    chunks = struct.pack("<II", len(text), CHUNK_JSON) + text
    if binary:
        binary += bytes(-len(binary) % 4)
        chunks += struct.pack("<II", len(binary), CHUNK_BIN) + binary
    header = struct.pack("<III", GLB_MAGIC, GLB_VERSION, 12 + len(chunks))
    return header + chunks


def glb_names(stem: str, switches: int) -> list[str]:
    """Return the .glb file names of a model: ``<stem>.glb``, ``<stem>_s1.glb``...

    One per switch index 0 to ``switches - 1`` (at least one). Does not
    check the stem.
    """
    return [f"{stem}.glb"] + [f"{stem}_s{s}.glb" for s in range(1, switches)]

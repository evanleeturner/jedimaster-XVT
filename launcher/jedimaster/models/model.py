"""The model as the game keeps it: node types, nodes, textures, faces.

Purpose:
    Name the node types of a model file and hold what the reader keeps of
    one: the file's version, base address and reserved number, its roots,
    and every node the game reaches with its payload; each texture's
    header, texels and palette; and the decoded form of the payloads the
    drawing needs (vectors, face records).

Flow:
    ``reader.read_opt`` builds an ``OptModel`` of ``OptNode`` and
    ``OptTexture`` records; ``vectors``, ``coords`` and ``face_data``
    decode a node's payload bytes on demand; ``draw`` and ``render`` read
    them.

Invariants:
    - Records are frozen; a node's ``payload`` is the bytes the game keeps,
      zeros where the body ended before the payload did.
    - Node numbers are the reader's first-visit order; a child or root of
      ``None`` is an empty slot.
    - Floats decode as the 4-byte floats they are, held as Python floats.

Call:
    ``model.nodes[n].type == TEXTURE``; ``face_data(node, model.version)``
"""

from __future__ import annotations

import logging
import struct
from dataclasses import dataclass

logger = logging.getLogger(__name__)

GROUP = 0
FACE_DATA = 1
TRANSFORM = 2
VERTICES = 3
TRANSLATION = 4
ROTATION = 5
SCALE = 6
NAME_REFERENCE = 7
DEFINITION = 8
MATERIALS = 9
MATERIAL_BINDING = 10
VERTEX_NORMALS = 11
NORMAL_BINDING = 12
TEXTURE_COORDINATES = 13
COORDINATE_BINDING = 14
INVENTOR_GROUP = 18
BASE_COLOR = 19
TEXTURE = 20
FACE_GROUP = 21
HARDPOINT = 22
ROTATION_SCALE = 23
NODE_SWITCH = 24
MESH_DESCRIPTOR = 25
FACE_TYPES = (FACE_DATA, 15, 16, 17)
"""The four face data types; all four carry faces laid out alike."""

FIXED_SIZES = {
    TRANSFORM: 48,
    TRANSLATION: 12,
    ROTATION: 36,
    SCALE: 12,
    BASE_COLOR: 12,
    HARDPOINT: 16,
    ROTATION_SCALE: 48,
    MESH_DESCRIPTOR: 72,
}
"""Payload bytes of the types whose payload has one size whatever the count."""
COUNTED_SIZES = {VERTICES: 12, MATERIALS: 56, VERTEX_NORMALS: 12}
COUNTED_SIZES |= {TEXTURE_COORDINATES: 8, FACE_GROUP: 4}
"""Payload bytes per counted item, for the types whose count sizes it."""
PAYLOAD_TYPES = frozenset(
    (*FIXED_SIZES, *COUNTED_SIZES, *FACE_TYPES, NAME_REFERENCE, TEXTURE)
)
"""Every type that has a payload; the others' payload link is ignored."""

RECORD_SIZES = {0: 48, 1: 64, 2: 64}
"""Bytes of one face record, by file version."""
NORMAL_BYTES = 12
GRADIENT_BYTES = 24
EDGE_COUNT_BYTES = 4


@dataclass(frozen=True)
class OptTexture:
    """A texture's header, texel bytes and the palette it uses.

    ``palette`` holds ``sub_palettes`` sub-palettes: their shading tables
    first (256 bytes each), then their colors (256 two-byte 565 values
    each, low byte first). ``palette_node`` is the node number of the
    texture that carries the block, or ``None`` for an inline palette.
    """

    palette_link: int
    inline_palettes: int
    texture_size: int
    data_size: int
    width: int
    height: int
    texels: bytes
    palette: bytes
    palette_node: int | None

    @property
    def top(self) -> bytes:
        """Return the top level's ``width * height`` texels, rows top first.

        Does not decode the smaller levels after it.
        """
        return self.texels[: self.width * self.height]

    @property
    def sub_palettes(self) -> int:
        """Return how many sub-palettes ``palette`` holds (16 for a block).

        Does not check that the palette is whole sub-palettes long.
        """
        return len(self.palette) // 768

    @property
    def shading(self) -> bytes:
        """Return the shading tables: 256 bytes per sub-palette; not used here."""
        return self.palette[: 256 * self.sub_palettes]

    def colors(self, sub_palette: int) -> list[int]:
        """Return one sub-palette's 256 colors as 565 values.

        Raises ``IndexError`` for a sub-palette the palette does not hold.
        Does not check the colors.
        """
        if not 0 <= sub_palette < self.sub_palettes:
            raise IndexError(f"no sub-palette {sub_palette} in {self.sub_palettes}")
        start = 256 * self.sub_palettes + 512 * sub_palette
        return list(struct.unpack_from("<256H", self.palette, start))

    def color_bytes(self, sub_palette: int) -> bytes:
        """Return one sub-palette's 512 color bytes as stored.

        Raises ``IndexError`` like ``colors``. Does not decode them.
        """
        self.colors(sub_palette)
        start = 256 * self.sub_palettes + 512 * sub_palette
        return self.palette[start : start + 512]


@dataclass(frozen=True)
class OptNode:
    """One node: its number, type, name, children and payload as kept.

    ``count`` is the payload count as the game keeps it (0 for a name
    reference); ``extra_normals`` is how many vertex normals follow a face
    data node's faces (0 when none, and for other types).
    """

    number: int
    offset: int
    type: int
    name: bytes | None
    children: tuple[int | None, ...]
    count: int
    payload: bytes
    extra_normals: int = 0
    texture: OptTexture | None = None
    reference: bytes | None = None


@dataclass(frozen=True)
class OptModel:
    """A model file as the game reads it, before any rewrite."""

    version: int
    base: int
    size: int
    reserved: int
    roots: tuple[int | None, ...]
    nodes: tuple[OptNode, ...]


@dataclass(frozen=True)
class FaceRecord:
    """One face: four corners' vertex, edge, coordinate and normal indices.

    ``normals`` repeats ``vertices`` in version 0, whose records hold no
    normal indices.
    """

    vertices: tuple[int, int, int, int]
    edges: tuple[int, int, int, int]
    coords: tuple[int, int, int, int]
    normals: tuple[int, int, int, int]
    normal: tuple[float, float, float]

    @property
    def corners(self) -> int:
        """Return 3 when the fourth vertex index is -1, else 4; no other check."""
        return 3 if self.vertices[3] == -1 else 4


@dataclass(frozen=True)
class FaceData:
    """A face data payload cut into its parts, as bytes and as records."""

    edge_count: int
    records: bytes
    normals: bytes
    gradients: bytes
    extra: bytes
    faces: tuple[FaceRecord, ...]


def vectors(node: OptNode) -> list[tuple[float, float, float]]:
    """Return a vertices or normals node's ``count`` x, y, z triples.

    Returns ``[]`` for a count of 0. Does not check the node's type.
    """
    return [struct.unpack_from("<3f", node.payload, 12 * i) for i in range(node.count)]


def coords(node: OptNode) -> list[tuple[float, float]]:
    """Return a texture coordinates node's ``count`` u, v pairs.

    Returns ``[]`` for a count of 0. Does not check the node's type.
    """
    return [struct.unpack_from("<2f", node.payload, 8 * i) for i in range(node.count)]


def floats(node: OptNode) -> tuple[float, ...]:
    """Return every whole 4-byte float of a node's payload, in order.

    Used for the fixed-size payloads and the face group's distances. Does
    not check the node's type.
    """
    return struct.unpack_from(f"<{len(node.payload) // 4}f", node.payload)


def face_data(node: OptNode, version: int) -> FaceData:
    """Return a face data node's payload cut into its parts.

    The edge count, ``count`` records (48 bytes in version 0, 64 in 1 and
    2), as many face normals and gradient pairs, then the extra vertex
    normals. Version 0 records repeat their vertex indices as normal
    indices. Does not check the node's type or the indices.
    """
    count, size = node.count, RECORD_SIZES[version]
    data = node.payload
    start = EDGE_COUNT_BYTES
    normals_at = start + count * size
    gradients_at = normals_at + count * NORMAL_BYTES
    extra_at = gradients_at + count * GRADIENT_BYTES
    faces = []
    groups = size // 16
    for i in range(count):
        ints = struct.unpack_from(f"<{4 * groups}i", data, start + i * size)
        quads = [tuple(ints[4 * g : 4 * g + 4]) for g in range(groups)]
        normal = struct.unpack_from("<3f", data, normals_at + i * NORMAL_BYTES)
        faces.append(FaceRecord(*quads[:3], quads[3 if groups > 3 else 0], normal))
    return FaceData(
        struct.unpack_from("<i", data, 0)[0],
        data[start:normals_at],
        data[normals_at:gradients_at],
        data[gradients_at:extra_at],
        data[extra_at : extra_at + node.extra_normals * NORMAL_BYTES],
        tuple(faces),
    )


def face_payload_size(version: int, count: int, extra_normals: int) -> int:
    """Return a face data payload's bytes: edge count, faces, extra normals.

    ``4 + count x (record + 36) + 12 x extra_normals``. Does not check the
    version.
    """
    per_face = RECORD_SIZES[version] + NORMAL_BYTES + GRADIENT_BYTES
    return EDGE_COUNT_BYTES + count * per_face + extra_normals * NORMAL_BYTES

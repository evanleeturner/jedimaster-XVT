"""Write synthetic model files (.opt): nodes, links and payloads laid out.

Purpose:
    Give the model tests files without game data. A model is described as
    ``N`` nodes (a type, a name, children, a payload count and payload) and
    ``Tex`` texture payloads; ``build_opt`` lays them out in a body at a
    chosen base address and writes every link as an address, so the tests
    check the reader against the layout rules rather than against itself.

Flow:
    ``build_opt`` collects the nodes depth first (each once, by identity),
    gives each its 24 bytes, then its name, its child table and its
    payload, in that order, and fills in the links once every place is
    known. The payload helpers pack vectors, coordinates, faces,
    hardpoints, descriptors and palettes.

Invariants:
    - Uses only the standard library and nothing from ``jedimaster``.
    - Payloads come last in the body, in node order, so the last node's
      payload is the one ``cut`` shortens.
    - Overrides (``name_link``, ``table_link``, ``payload_link``,
      ``Tex.palette_link``, ``root_links``, ``marker``, ``size``) write a
      raw value where the layout would write its own.

Call:
    ``data = build_opt([N(0, children=[N(3, count=1, payload=vecs([(1, 2, 3)]))])])``
"""

from __future__ import annotations

import logging
import struct
from dataclasses import dataclass
from dataclasses import field

logger = logging.getLogger(__name__)

HEADER = 14
NODE = 24


@dataclass(eq=False)
class Tex:
    """A texture payload: header fields, texels, and where its palette lies.

    ``palette`` is the block or inline palette the texture carries after
    its texels (``None``: it carries none); ``shares`` is the node whose
    block its link names.
    """

    width: int
    height: int
    texels: bytes
    texture_size: int | None = None
    data_size: int | None = None
    inline: int = 0
    palette: bytes | None = None
    shares: N | None = None
    palette_link: int | None = None


@dataclass(eq=False)
class N:
    """A node: type, name, children (``N`` or ``None``), count and payload."""

    type: int
    name: bytes | None = None
    children: list[N | None] = field(default_factory=list)
    count: int = 1
    payload: bytes | Tex | None = None
    name_link: int | None = None
    table_link: int | None = None
    payload_link: int | None = None
    child_count: int | None = None


def _collect(roots: list[N | None]) -> list[N]:
    order: list[N] = []
    seen: set[int] = set()
    stack = [r for r in reversed(roots) if r is not None]
    while stack:
        node = stack.pop()
        if id(node) in seen:
            continue
        seen.add(id(node))
        order.append(node)
        stack += [c for c in reversed(node.children) if c is not None]
    return order


def _texture_bytes(tex: Tex, link: int) -> bytes:
    pixels = tex.width * tex.height
    header = struct.pack(
        "<I5i",
        link,
        tex.inline,
        pixels if tex.texture_size is None else tex.texture_size,
        len(tex.texels) if tex.data_size is None else tex.data_size,
        tex.width,
        tex.height,
    )
    return header + tex.texels + (tex.palette or b"")


class _Layout:
    """Where each node's record, name, child table and payload lie."""

    def __init__(self, roots: list[N | None], base: int) -> None:
        self.base = base
        self.nodes = _collect(roots)
        at = HEADER + 4 * len(roots)
        self.places: dict[int, int] = {}
        for node in self.nodes:
            self.places[id(node)] = at
            at += NODE
        self.names: dict[int, int] = {}
        for node in self.nodes:
            if node.name is not None:
                self.names[id(node)] = at
                at += len(node.name) + 1
        self.tables: dict[int, int] = {}
        for node in self.nodes:
            if node.children:
                self.tables[id(node)] = at
                at += 4 * len(node.children)
        self.payloads: dict[int, int] = {}
        for node in self.nodes:
            blob = node.payload
            if blob is not None:
                self.payloads[id(node)] = at
                at += (
                    len(_texture_bytes(blob, 0)) if isinstance(blob, Tex) else len(blob)
                )
        self.size = at

    def addr(self, table: dict[int, int], node: N) -> int:
        """Return the address of ``node``'s entry in ``table``, or 0 for none."""
        return self.base + table[id(node)] if id(node) in table else 0

    def tex_end(self, node: N) -> int:
        """Return the address right after a texture node's texels."""
        tex = node.payload
        assert isinstance(tex, Tex)
        return self.base + self.payloads[id(node)] + 24 + len(tex.texels)

    def payload_bytes(self, node: N) -> bytes | None:
        """Return a node's payload with its palette link filled in."""
        blob = node.payload
        if not isinstance(blob, Tex):
            return blob
        if blob.palette_link is not None:
            link = blob.palette_link
        elif blob.shares is not None:
            link = self.tex_end(blob.shares)
        else:
            link = self.tex_end(node) if blob.palette is not None else 0
        return _texture_bytes(blob, link)

    def write(self, body: bytearray, node: N) -> None:
        """Write one node's record, name, child table and payload."""
        name_link = node.name_link
        if name_link is None:
            name_link = self.addr(self.names, node)
        table = (
            node.table_link
            if node.table_link is not None
            else self.addr(self.tables, node)
        )
        link = node.payload_link
        if link is None:
            link = self.addr(self.payloads, node)
        count = len(node.children) if node.child_count is None else node.child_count
        record = struct.pack(
            "<IiiIiI", name_link, node.type, count, table, node.count, link
        )
        body[self.places[id(node)] : self.places[id(node)] + NODE] = record
        if node.name is not None:
            start = self.names[id(node)]
            body[start : start + len(node.name)] = node.name
        if node.children:
            links = [
                0 if c is None else self.addr(self.places, c) for c in node.children
            ]
            struct.pack_into(f"<{len(links)}I", body, self.tables[id(node)], *links)
        blob = self.payload_bytes(node)
        if blob is not None:
            start = self.payloads[id(node)]
            body[start : start + len(blob)] = blob


def build_opt(
    roots: list[N | None],
    version: int = 2,
    base: int = 0x10000,
    reserved: int = 7,
    *,
    root_links: list[int] | None = None,
    root_count: int | None = None,
    root_table: int | None = None,
    marker: int | None = None,
    size: int | None = None,
    cut: int = 0,
    tail: bytes = b"",
) -> bytes:
    """Return a model file holding ``roots`` and every node below them.

    ``version`` 2 by default: drawn as it is, no load-time rewrite.
    ``cut`` drops that many bytes from the body's end (shortening the last
    payload); ``tail`` adds bytes after the body that the size does not
    count. Does not check that the model is one the game accepts.
    """
    layout = _Layout(roots, base)
    body = bytearray(layout.size)
    for node in layout.nodes:
        layout.write(body, node)
    links = root_links
    if links is None:
        links = [0 if r is None else layout.addr(layout.places, r) for r in roots]
    struct.pack_into(f"<{len(links)}I", body, HEADER, *links)
    count = len(roots) if root_count is None else root_count
    table_link = base + HEADER if root_table is None else root_table
    struct.pack_into("<IHiI", body, 0, base, reserved, count, table_link)
    data = bytes(body[: len(body) - cut] if cut else body)
    length = len(data) if size is None else size
    if version == 0:
        head = struct.pack("<i", length if marker is None else marker)
    else:
        head = struct.pack("<ii", -version if marker is None else marker, length)
    return head + data + tail


def vecs(values: list[tuple[float, float, float]]) -> bytes:
    """Return x, y, z floats for each vector (a vertices or normals payload)."""
    return b"".join(struct.pack("<3f", *v) for v in values)


def uvs(values: list[tuple[float, float]]) -> bytes:
    """Return u, v floats for each pair (a texture coordinates payload)."""
    return b"".join(struct.pack("<2f", *v) for v in values)


@dataclass
class Face:
    """One face record: up to four corners' indices, and its face normal."""

    vertices: tuple[int, ...]
    coords: tuple[int, ...] = (0, 0, 0, 0)
    normals: tuple[int, ...] = (0, 0, 0, 0)
    normal: tuple[float, float, float] = (0.0, 0.0, 1.0)
    edges: tuple[int, ...] = (0, 0, 0, 0)


def _four(values: tuple[int, ...]) -> tuple[int, ...]:
    return tuple(values) + (-1,) * (4 - len(values))


def faces(
    version: int,
    records: list[Face],
    edges: int = 1,
    extra: list[tuple[float, float, float]] | None = None,
    gradient: float = 0.5,
) -> bytes:
    """Return a face data payload: edge count, records, normals, gradients, extra.

    Version 0 records hold three index groups (48 bytes), versions 1 and 2
    four (64 bytes). ``extra`` are the vertex normals stored after them.
    """
    out = struct.pack("<i", edges)
    for face in records:
        groups = [_four(face.vertices), _four(face.edges), _four(face.coords)]
        if version:
            groups.append(_four(face.normals))
        out += b"".join(struct.pack("<4i", *g) for g in groups)
    out += b"".join(struct.pack("<3f", *f.normal) for f in records)
    out += struct.pack("<6f", *(gradient,) * 6) * len(records)
    return out + vecs(extra or [])


def hardpoint(kind: int, at: tuple[float, float, float]) -> bytes:
    """Return a hardpoint payload: a type, then a position."""
    return struct.pack("<i3f", kind, *at)


def descriptor(mesh_type: int, flags: int, target_id: int, seed: float = 1.0) -> bytes:
    """Return a mesh descriptor payload: 12 distinct floats around the numbers."""
    values = [seed + k for k in range(12)]
    target = (seed + 20, seed + 21, seed + 22)
    return struct.pack("<2i12fi3f", mesh_type, flags, *values, target_id, *target)


def palette(sub_palettes: int = 16, color=None, shade: int = 0) -> bytes:
    """Return a palette: ``sub_palettes`` shading tables, then their colors.

    ``color(sub, index)`` gives each 565 color (default: a distinct value
    per sub-palette and index); each shading byte is ``shade``.
    """
    pick = color or (lambda sub, index: (sub * 4099 + index * 37) & 0xFFFF)
    colors = b"".join(
        struct.pack("<H", pick(sub, index))
        for sub in range(sub_palettes)
        for index in range(256)
    )
    return bytes([shade]) * (256 * sub_palettes) + colors

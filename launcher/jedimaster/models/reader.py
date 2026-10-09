"""Read a model file (.opt) the way the game reads it: header, nodes, payloads.

Purpose:
    Turn the bytes of an X-Wing vs. TIE Fighter or Balance of Power model
    file into an ``OptModel``: its version, base address, reserved number,
    roots, and every node the game reaches, numbered in the order it first
    reaches them, each with its payload as the game keeps it. Refuse, with
    a ``ModelFormatError`` naming the rule, what the game refuses.

Flow:
    ``read_opt`` checks the marker and the body's size and reads the
    body's header; ``_Walker`` visits the roots in order, depth first,
    each node once: its 24 bytes, its name, its payload, its children.
    It carries the vertex count and whether a vertex normals node was met,
    which size a face data payload. ``textures.read_texture`` reads each
    texture; ``textures.link_palettes`` gives each one its palette once
    every node is known.

Invariants:
    - A link is an address: ``A`` names body offset ``A - base``; 0 is
      none. A link the game follows must fall inside the body.
    - Nothing is read outside the body: a payload cut by the body's end
      reads as zero bytes there; a node, a table, a name or a texture that
      does not fit is refused.
    - Limits: 65,536 roots, children per node and nodes; 256 levels of
      nesting (a root is level 1); a payload count from 0 to 1,000,000.

Call:
    ``model = read_opt(path); model.nodes[model.roots[0]].type``
"""

from __future__ import annotations

import logging
import os
import struct
from dataclasses import dataclass
from dataclasses import replace

from .body import Body
from .body import ModelFormatError
from .model import COUNTED_SIZES
from .model import face_payload_size
from .model import FACE_TYPES
from .model import FIXED_SIZES
from .model import NAME_REFERENCE
from .model import OptModel
from .model import OptNode
from .model import PAYLOAD_TYPES
from .model import TEXTURE
from .model import VERTEX_NORMALS
from .model import VERTICES
from .textures import link_palettes
from .textures import read_texture

logger = logging.getLogger(__name__)

BODY_MIN = 14
BODY_MAX = 128 * 1024 * 1024
ADDRESS_MAX = 0xFFFFFFFF
MAX_ROOTS = 65536
MAX_CHILDREN = 65536
MAX_NODES = 65536
MAX_DEPTH = 256
MAX_PAYLOAD_COUNT = 1_000_000
NODE_SIZE = 24


@dataclass
class _Context:
    """What the reader carries down the graph to size face data payloads."""

    vertex_count: int = 0
    normals_met: bool = False


class _Walker:
    """Visit the nodes depth first, each once, numbering them as reached."""

    def __init__(self, body: Body, version: int) -> None:
        self.body = body
        self.version = version
        self.numbers: dict[int, int] = {}
        self.nodes: list[OptNode] = []
        self.inside: set[int] = set()

    def visit(self, link: int, depth: int, context: _Context) -> tuple[int, _Context]:
        """Visit the node at ``link``; return its number and the context after it.

        The context after a node is what its later siblings start from:
        changed by the node itself, never by its children.
        """
        offset = self.body.offset(link, "node")
        if offset in self.inside:
            raise ModelFormatError(f"node at body offset {offset} is its own ancestor")
        if depth > MAX_DEPTH:
            raise ModelFormatError(f"nesting deeper than {MAX_DEPTH} levels")
        raw = self.body.need(offset, NODE_SIZE, "node")
        name_link, kind, child_count, table, count, payload_link = struct.unpack(
            "<IiiIiI", raw
        )
        context = _effect(kind, count, context)
        if offset in self.numbers:
            logger.debug("node at %d reached again", offset)
            return self.numbers[offset], context
        number = len(self.nodes)
        if number >= MAX_NODES:
            raise ModelFormatError(f"more than {MAX_NODES} nodes")
        logger.debug(
            "node %d at %d: type %d, %d children, count %d, name at %#x, payload at %#x",
            number,
            offset,
            kind,
            child_count,
            count,
            name_link,
            payload_link,
        )
        self.numbers[offset] = number
        name = None
        if name_link:
            name = self.body.string(self.body.offset(name_link, "name"), "name")
        node = self._payload(number, offset, kind, name, count, payload_link, context)
        self.nodes.append(node)
        links = self._child_links(child_count, table)
        self.inside.add(offset)
        children: list[int | None] = []
        inner = context
        for child in links:
            if child == 0:
                children.append(None)
                continue
            child_number, inner = self.visit(child, depth + 1, inner)
            children.append(child_number)
        self.inside.discard(offset)
        self.nodes[number] = replace(self.nodes[number], children=tuple(children))
        return number, context

    def _child_links(self, count: int, table: int) -> tuple[int, ...]:
        if not 0 <= count <= MAX_CHILDREN:
            raise ModelFormatError(f"child count {count} is not 0 to {MAX_CHILDREN}")
        if count == 0:
            return ()
        offset = self.body.offset(table, "child table")
        return struct.unpack(
            f"<{count}I", self.body.need(offset, 4 * count, "child table")
        )

    def _payload(
        self,
        number: int,
        offset: int,
        kind: int,
        name: bytes | None,
        count: int,
        link: int,
        context: _Context,
    ) -> OptNode:
        node = OptNode(number, offset, kind, name, (), count, b"")
        if kind not in PAYLOAD_TYPES:
            logger.debug("node %d type %d: no payload", number, kind)
            return node
        if not 0 <= count <= MAX_PAYLOAD_COUNT:
            raise ModelFormatError(
                f"node {number} payload count {count} is not 0 to {MAX_PAYLOAD_COUNT}"
            )
        start = self.body.offset(link, f"node {number} payload")
        if kind == TEXTURE:
            texture, payload = read_texture(self.body, start, number)
            return replace(node, payload=payload, texture=texture)
        if kind == NAME_REFERENCE:
            end = self.body.data.find(b"\0", start)
            name_ref = self.body.data[start : end if end >= 0 else None]
            logger.debug("node %d refers to %r", number, name_ref)
            return replace(node, count=0, payload=name_ref, reference=name_ref)
        extra = 0
        if kind in FACE_TYPES:
            extra = 0 if context.normals_met else context.vertex_count
            size = face_payload_size(self.version, count, extra)
        elif kind in FIXED_SIZES:
            size = FIXED_SIZES[kind]
        else:
            size = COUNTED_SIZES[kind] * count
        if start + size > self.body.size:
            logger.debug("node %d payload cut by the body's end", number)
        payload = self.body.padded(start, size)
        logger.debug("node %d type %d: %d payload bytes", number, kind, size)
        return replace(node, payload=payload, extra_normals=extra)


def _effect(kind: int, count: int, context: _Context) -> _Context:
    """Return the context after a node: a vertex count or 'normals met' set."""
    if kind == VERTICES:
        return _Context(count, context.normals_met)
    if kind == VERTEX_NORMALS:
        return _Context(context.vertex_count, True)
    return context


def split_file(data: bytes) -> tuple[int, bytes]:
    """Return a model file's version and body, checking the marker and size.

    A positive marker is version 0 and the body's size; -1 and -2 are
    versions 1 and 2, with the size in the next 4 bytes. Raises
    ``ModelFormatError`` for any other marker, a size outside 14 bytes to
    128 MiB, or a size other than what remains of the file. Does not read
    the body.
    """
    if len(data) < 4:
        raise ModelFormatError("file is shorter than its 4-byte marker")
    marker = struct.unpack_from("<i", data)[0]
    if marker > 0:
        version, size, start = 0, marker, 4
    elif marker in (-1, -2):
        if len(data) < 8:
            raise ModelFormatError("file is shorter than its marker and size")
        version, size, start = -marker, struct.unpack_from("<i", data, 4)[0], 8
    else:
        raise ModelFormatError(f"marker {marker} is not a size, -1 or -2")
    if not BODY_MIN <= size <= BODY_MAX:
        raise ModelFormatError(f"body size {size} is not {BODY_MIN} to {BODY_MAX}")
    if size != len(data) - start:
        raise ModelFormatError(
            f"body size {size} is not the {len(data) - start} bytes that remain"
        )
    return version, data[start:]


def read_opt(source: str | os.PathLike[str] | bytes) -> OptModel:
    """Return the model a file holds, read the way the game reads it.

    ``source`` is a path or the file's bytes. Raises ``ModelFormatError``
    naming the rule for anything the game refuses, and ``OSError`` when a
    path cannot be read. Does not regroup faces (``draw`` does) or check
    what the game never checks (types it does not know, unused links).
    """
    data = source if isinstance(source, bytes) else _read(source)
    version, raw = split_file(data)
    base = struct.unpack_from("<I", raw)[0]
    if base + len(raw) > ADDRESS_MAX:
        raise ModelFormatError(f"base {base:#x} plus size {len(raw)} passes 2^32 - 1")
    body = Body(raw, base)
    reserved = struct.unpack_from("<H", raw, 4)[0]
    root_count = body.int32(6, "root count")
    logger.debug(
        "body: %d bytes at base %#x, reserved %d, %d roots, version %d",
        len(raw),
        base,
        reserved,
        root_count,
        version,
    )
    if not 0 <= root_count <= MAX_ROOTS:
        raise ModelFormatError(f"root count {root_count} is not 0 to {MAX_ROOTS}")
    links: tuple[int, ...] = ()
    if root_count:
        table = body.offset(body.uint32(10, "root table link"), "root table")
        links = struct.unpack(
            f"<{root_count}I", body.need(table, 4 * root_count, "root table")
        )
    walker = _Walker(body, version)
    roots: list[int | None] = []
    context = _Context()
    for link in links:
        if link == 0:
            roots.append(None)
            continue
        number, context = walker.visit(link, 1, context)
        roots.append(number)
    nodes = link_palettes(body, walker.nodes)
    logger.info(
        "model: version %d, %d roots, %d nodes, %d bytes",
        version,
        len(roots),
        len(nodes),
        len(raw),
    )
    return OptModel(version, base, len(raw), reserved, tuple(roots), tuple(nodes))


def _read(path: str | os.PathLike[str]) -> bytes:
    with open(path, "rb") as handle:
        data = handle.read()
    logger.debug("read %s: %d bytes", path, len(data))
    return data

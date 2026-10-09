"""The rewrite a version 0 or 1 model gets when it loads: faces regrouped.

Purpose:
    Find the node a name reference stands for (``Resolver``), and move
    faces between face data nodes the way the game rewrites a version 0
    or 1 model before drawing it: a face data node gathers the later face
    data nodes of its detail level that follow the same last texture.

Flow:
    ``regroup`` walks the whole graph once (``_Rewrite.visit``): every
    child of every node, depth first, without following name references
    into the nodes they name, keeping the last texture seen and the last
    face group passed. At each face data node ``F`` still holding its
    faces, ``_Rewrite.gather`` walks the child of the last face group that
    holds ``F`` from its beginning, following name references, and takes
    from ``F`` on every face data node with an edge count above 0 and
    ``F``'s last texture; the taken nodes are emptied and ``F`` holds them.

Invariants:
    - A version 2 model is drawn as it is: every node keeps its faces.
    - A face keeps its record: its indices, its face normal, and the
      extra normals stored after the node it came from. A face moved from
      a later node is resolved with the vertex, texture coordinate and
      vertex normal lists the gathering walk had at that node: the lists
      current at ``F`` in the whole-graph walk, changed by every such
      node the gathering walk passes.
    - A face data node outside every child of the last face group passed
      gathers nothing and loses its own faces: it holds none. One met
      before any face group is refused (``ModelFormatError``).
    - A name reference whose chain returns to a name already looked up,
      or to a node being walked, stands for nothing; a gathering walk goes
      no deeper than ``WALK_LIMIT`` nodes.

Call:
    ``lists = regroup(model); lists.get(node_number)`` -> ``(FaceRef, ...)``
"""

from __future__ import annotations

import logging
import struct
from typing import NamedTuple

from .body import ModelFormatError
from .model import FACE_GROUP
from .model import FACE_TYPES
from .model import NAME_REFERENCE
from .model import OptModel
from .model import TEXTURE
from .model import TEXTURE_COORDINATES
from .model import VERTEX_NORMALS
from .model import VERTICES

logger = logging.getLogger(__name__)

WALK_LIMIT = 512
"""The deepest a gathering walk goes (the drawing walk's limit)."""
REGROUPED_VERSIONS = (0, 1)
LIST_SLOTS = {VERTICES: 0, TEXTURE_COORDINATES: 1, VERTEX_NORMALS: 2}
"""Where each list node goes in a ``Lists`` triple."""
Lists = tuple[int | None, int | None, int | None]
"""The vertices, texture coordinates and vertex normals nodes, or None each."""


class FaceRef(NamedTuple):
    """A face: the node its record is stored in, its index there, its lists.

    ``lists`` is None for a face the drawing walk resolves with its own
    state, else the list nodes a moved face is resolved with.
    """

    source: int
    index: int
    lists: Lists | None = None


def _with_list(lists: Lists, kind: int, number: int) -> Lists:
    """Return ``lists`` with node ``number`` in the slot of its type."""
    out = list(lists)
    out[LIST_SLOTS[kind]] = number
    return (out[0], out[1], out[2])


class Resolver:
    """Find the node a name stands for, as the walk of a name reference does."""

    def __init__(self, model: OptModel) -> None:
        self.model = model
        self.by_name: dict[bytes, int] = {}
        for node in model.nodes:
            if node.name is not None:
                self.by_name.setdefault(node.name.lower(), node.number)

    def resolve(self, name: bytes) -> int | None:
        """Return the number of the node ``name`` stands for, or None.

        The first node in the reader's order (the roots in order, each
        depth first, references not followed) whose name equals ``name``
        ignoring ASCII letter case; when that node is itself a reference,
        the search runs again with the name it refers to. Returns None
        when nothing is found or the chain comes back to a node it met.
        Does not check the found node's type.
        """
        seen: set[int] = set()
        key = name.lower()
        while True:
            number = self.by_name.get(key)
            if number is None or number in seen:
                logger.debug("name %r stands for nothing", name)
                return None
            node = self.model.nodes[number]
            if node.type != NAME_REFERENCE:
                return number
            seen.add(number)
            key = (node.reference or b"").lower()

    def texture(self, name: bytes) -> int | None:
        """Return the texture node ``name`` stands for, or None for any other.

        None when the name stands for nothing or for a node of another
        type. Does not walk the node.
        """
        number = self.resolve(name)
        if number is None or self.model.nodes[number].type != TEXTURE:
            return None
        return number


def _edge_count(payload: bytes) -> int:
    return struct.unpack_from("<i", payload.ljust(4, b"\0"))[0]


class _Rewrite:
    """The rewrite's state: each face data node's faces and the walk's marks."""

    def __init__(self, model: OptModel, resolver: Resolver) -> None:
        self.model = model
        self.resolver = resolver
        self.lists: dict[int, list[FaceRef]] = {
            n.number: [FaceRef(n.number, i) for i in range(n.count)]
            for n in model.nodes
            if n.type in FACE_TYPES
        }
        self.edges = {n: _edge_count(model.nodes[n].payload) for n in self.lists}
        self.emptied: set[int] = set()
        self.last_texture: int | None = None
        self.last_group: int | None = None
        self.current: Lists = (None, None, None)
        self.path: list[int] = []

    def visit(self, number: int) -> None:
        """Walk one node and all its children, gathering at face data nodes."""
        node = self.model.nodes[number]
        self.path.append(number)
        if node.type == FACE_GROUP:
            self.last_group = number
        elif node.type == TEXTURE:
            self.last_texture = number
        elif node.type == NAME_REFERENCE:
            target = self.resolver.texture(node.reference or b"")
            if target is not None:
                self.last_texture = target
        elif node.type in LIST_SLOTS:
            self.current = _with_list(self.current, node.type, number)
        elif node.type in FACE_TYPES and number not in self.emptied:
            self.gather(number)
        for child in node.children:
            if child is not None:
                self.visit(child)
        self.path.pop()

    def holder(self) -> int | None:
        """Return the child of the last face group that holds the current node."""
        if self.last_group is None or self.last_group not in self.path:
            return None
        at = len(self.path) - 1 - self.path[::-1].index(self.last_group)
        return self.path[at + 1] if at + 1 < len(self.path) else None

    def gather(self, first: int) -> None:
        """Let face data node ``first`` gather its level's matching faces."""
        holder = self.holder()
        if holder is None:
            if self.last_group is None:
                raise ModelFormatError(
                    f"face data node {first} comes before any face group"
                )
            logger.debug("face data %d: in no level child, its faces lost", first)
            self.lists[first] = []
            return
        sweep = _Sweep(self, first, self.last_texture)
        sweep.walk(holder)
        self.lists[first] = sweep.gathered
        self.emptied.discard(first)
        logger.debug(
            "face data %d gathered %d faces from %s",
            first,
            len(sweep.gathered),
            sweep.taken,
        )


class _Sweep:
    """One gathering walk of a level child, from its beginning."""

    def __init__(self, rewrite: _Rewrite, first: int, texture: int | None) -> None:
        self.rewrite = rewrite
        self.first = first
        self.texture = texture
        self.last_texture = texture
        self.started = False
        self.lists = rewrite.current
        self.gathered: list[FaceRef] = []
        self.taken: list[int] = []
        self.active: set[int] = set()

    def walk(self, number: int) -> None:
        """Walk a node, following references, taking matching faces from ``first``."""
        model = self.rewrite.model
        node = model.nodes[number]
        if node.type == NAME_REFERENCE:
            target = self.rewrite.resolver.resolve(node.reference or b"")
            if target is None or target in self.active:
                return
            number, node = target, model.nodes[target]
        if len(self.active) >= WALK_LIMIT:
            return
        self.active.add(number)
        if node.type == TEXTURE:
            self.last_texture = number
        elif node.type in LIST_SLOTS:
            self.lists = _with_list(self.lists, node.type, number)
        elif node.type in FACE_TYPES:
            self.take(number)
        for child in node.children:
            if child is not None:
                self.walk(child)
        self.active.discard(number)

    def take(self, number: int) -> None:
        """Take a face data node's faces when the rule allows it."""
        rewrite = self.rewrite
        if number == self.first:
            self.started = True
        if (
            self.started
            and number not in rewrite.emptied
            and rewrite.edges[number] > 0
            and self.last_texture == self.texture
        ):
            faces = rewrite.lists[number]
            if number != self.first:
                faces = [f._replace(lists=f.lists or self.lists) for f in faces]
            self.gathered += faces
            rewrite.lists[number] = []
            rewrite.emptied.add(number)
            self.taken.append(number)


def regroup(
    model: OptModel, resolver: Resolver | None = None
) -> dict[int, tuple[FaceRef, ...]]:
    """Return each face data node's faces after the load-time rewrite.

    For version 0 and 1 the rewrite of the whole graph; for version 2 (and
    any other) every node keeps its own faces. A node emptied by the
    rewrite is left out: it is no longer a mesh. A node that still holds
    its own (possibly no) faces maps to them. Does not draw or check any
    index.
    """
    resolver = resolver or Resolver(model)
    rewrite = _Rewrite(model, resolver)
    if model.version in REGROUPED_VERSIONS:
        for root in model.roots:
            if root is not None:
                rewrite.visit(root)
    moved = sum(
        1 for n, faces in rewrite.lists.items() for face in faces if face.source != n
    )
    logger.debug(
        "regroup: %d faces moved, %d nodes emptied", moved, len(rewrite.emptied)
    )
    return {
        n: tuple(faces)
        for n, faces in rewrite.lists.items()
        if n not in rewrite.emptied
    }

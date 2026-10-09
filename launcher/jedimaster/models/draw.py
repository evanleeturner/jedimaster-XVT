"""What the game draws of a model: its faces per component, for a level and switch.

Purpose:
    Walk a model's graph the way the game draws it, for one detail level
    and one switch index, and give every face drawn per component: its
    texture (a texture node, or the built-in white one), its corners'
    positions, texture coordinates and normals, and its face normal.

Flow:
    ``regroup.regroup`` first rewrites the face lists of a version 0 or 1
    model; ``draw_pass`` then walks the roots in order with one shared
    state (``_Walk``): name references stand for the node they name; a
    face group picks the child of the level, a node switch the child of
    the switch; any other node walks its children with one copy of the
    state. ``draw_model`` runs every pass the sheets print.

Invariants:
    - The state's lists and texture start empty; the last texture starts
      as the built-in white texture and is never reset within a pass.
    - A value whose index lies outside its list is ``None`` (``bad``).
    - Transforms, translations, rotations and scales are not applied:
      positions are as stored.
    - A name reference whose chain comes back to a node being walked
      draws nothing there, and a walk nested deeper than ``WALK_LIMIT``
      nodes (only name references nest that deep) draws nothing below, so
      a walk always ends.

Call:
    ``drawn = draw_pass(model, lod=1, switch=0); drawn.components[0].faces``
"""

from __future__ import annotations

import logging
import struct
from dataclasses import dataclass
from dataclasses import field

from .model import coords as coord_pairs
from .model import face_data
from .model import FACE_GROUP
from .model import FACE_TYPES
from .model import FaceData
from .model import NAME_REFERENCE
from .model import NODE_SWITCH
from .model import OptModel
from .model import TEXTURE
from .model import TEXTURE_COORDINATES
from .model import vectors
from .model import VERTEX_NORMALS
from .model import VERTICES
from .regroup import FaceRef
from .regroup import Lists
from .regroup import regroup
from .regroup import Resolver

logger = logging.getLogger(__name__)

TEXTURE_ROOTS = -1
"""The component number of faces drawn under a texture root."""
WALK_LIMIT = 512
"""The deepest a walk goes: twice the reader's nesting limit."""

Vector = tuple[float, float, float]


@dataclass(frozen=True)
class Corner:
    """One corner of a drawn face; ``None`` for a value whose index is bad."""

    position: Vector | None
    uv: tuple[float, float] | None
    normal: Vector | None


@dataclass(frozen=True)
class DrawnFace:
    """One face as drawn: texture, corners, face normal, and where it came from.

    ``texture`` is a texture node's number, ``None`` for the built-in white
    texture. ``named`` is False when the state had no texture and the face
    took the last texture (its name is then not set, and it draws no
    glow). ``node`` is the face data node that drew it, ``source`` the node
    its record was read from (they differ for a face the rewrite moved).
    """

    texture: int | None
    named: bool
    corners: tuple[Corner, ...]
    normal: Vector
    node: int
    source: int


@dataclass(frozen=True)
class DrawnComponent:
    """The faces one component drew, in drawing order."""

    number: int
    faces: tuple[DrawnFace, ...]


@dataclass(frozen=True)
class DrawPass:
    """One pass: its level and switch, each component's faces, two counts.

    ``meshes`` counts the face data nodes the walk reached that the
    rewrite did not empty (one that holds no face counts too);
    ``missing`` the name references walked that stand for nothing.
    """

    lod: int
    switch: int
    components: tuple[DrawnComponent, ...]
    meshes: int
    missing: int = 0


@dataclass
class _State:
    """The walk's state: current lists and texture (copied for children)."""

    vertices: list[Vector] | None = None
    normals: list[Vector] | None = None
    coords: list[tuple[float, float]] | None = None
    texture: int | None = None
    has_texture: bool = False
    named: bool = False

    def copy(self) -> _State:
        return _State(
            self.vertices,
            self.normals,
            self.coords,
            self.texture,
            self.has_texture,
            self.named,
        )


@dataclass
class _Walk:
    """One pass's walk over a model; collects faces into ``faces``."""

    model: OptModel
    lod: int
    switch: int
    lists: dict[int, tuple[FaceRef, ...]]
    resolver: Resolver
    data: dict[int, tuple[FaceData, list[Vector]]] = field(default_factory=dict)
    faces: list[DrawnFace] = field(default_factory=list)
    meshes: int = 0
    missing: int = 0
    last_texture: int | None = None
    active: set[int] = field(default_factory=set)
    decoded: dict[int, list] = field(default_factory=dict)

    def walk(self, number: int, state: _State) -> None:
        """Walk one node with ``state``: its effect, then its children."""
        node = self.model.nodes[number]
        if node.type == NAME_REFERENCE:
            target = self.resolver.resolve(node.reference or b"")
            if target is None or target in self.active:
                logger.debug("reference %r at node %d: nothing", node.reference, number)
                self.missing += target is None
                return
            number, node = target, self.model.nodes[target]
        if len(self.active) >= WALK_LIMIT:
            logger.debug(
                "walk deeper than %d nodes at node %d: cut", WALK_LIMIT, number
            )
            return
        self.active.add(number)
        picked = self._effect(number, state)
        if node.type in (FACE_GROUP, NODE_SWITCH):
            if picked is not None:
                self.walk(picked, state)
        else:
            inner = state.copy()
            for child in node.children:
                if child is not None:
                    self.walk(child, inner)
        self.active.discard(number)

    def _effect(self, number: int, state: _State) -> int | None:
        """Apply a node's own effect; return the child a group or switch picks."""
        node = self.model.nodes[number]
        kind = node.type
        if kind == VERTICES:
            state.vertices = vectors(node)
        elif kind == VERTEX_NORMALS:
            state.normals = vectors(node)
        elif kind == TEXTURE_COORDINATES:
            state.coords = coord_pairs(node)
        elif kind == TEXTURE:
            state.texture, state.has_texture, state.named = number, True, True
            self.last_texture = number
        elif kind in FACE_TYPES:
            self._draw(number, state)
        elif kind == FACE_GROUP:
            children = node.children
            return children[self.lod - 1] if len(children) >= self.lod else None
        elif kind == NODE_SWITCH:
            children = node.children
            if not children:
                return None
            return children[min(self.switch, len(children) - 1)]
        return None

    def _draw(self, number: int, state: _State) -> None:
        if not state.has_texture:
            state.texture, state.has_texture = self.last_texture, True
            state.named = False
        refs = self.lists.get(number)
        if refs is None:
            logger.debug("face data %d: emptied by the rewrite", number)
            return
        self.meshes += 1
        for source, index, lists in refs:
            if source not in self.data:
                data = face_data(self.model.nodes[source], self.model.version)
                self.data[source] = (data, _triples(data.extra))
            data, extra = self.data[source]
            use = state if lists is None else self._bound(lists, state)
            self.faces.append(_face(data, extra, index, use, (number, source)))

    def _bound(self, lists: Lists, state: _State) -> _State:
        """Return the state a moved face is resolved with: its own lists."""
        decoded = []
        for number in lists:
            if number is not None and number not in self.decoded:
                node = self.model.nodes[number]
                kind = node.type == TEXTURE_COORDINATES
                self.decoded[number] = coord_pairs(node) if kind else vectors(node)
            decoded.append(None if number is None else self.decoded[number])
        vertices, coords, normals = decoded
        return _State(vertices, normals, coords, state.texture, True, state.named)


def _pick(values: list | None, index: int):
    """Return ``values[index]``, or None for no list or an index outside it."""
    if values is None or not 0 <= index < len(values):
        return None
    return values[index]


def _triples(data: bytes) -> list[Vector]:
    return [struct.unpack_from("<3f", data, 12 * i) for i in range(len(data) // 12)]


def _face(
    data: FaceData,
    extra: list[Vector],
    index: int,
    state: _State,
    nodes: tuple[int, int],
) -> DrawnFace:
    """Return one face record drawn with the state's lists and texture.

    Normals come from the state's normal list, or, with none, from
    ``extra``: the normals stored after the node the record came from.
    ``nodes`` is the drawing node and the source node.
    """
    record = data.faces[index]
    normals = state.normals if state.normals is not None else extra
    corners = tuple(
        Corner(
            _pick(state.vertices, record.vertices[k]),
            _pick(state.coords, record.coords[k]),
            _pick(normals, record.normals[k]),
        )
        for k in range(record.corners)
    )
    return DrawnFace(state.texture, state.named, corners, record.normal, *nodes)


def most_children(model: OptModel, kind: int) -> int:
    """Return the most children any node of type ``kind`` has, at least 1.

    Counts empty slots as children. Does not check that the nodes are
    reachable in any pass (every node of the model is).
    """
    counts = [len(n.children) for n in model.nodes if n.type == kind]
    return max([1, *counts])


def components(model: OptModel) -> list[int]:
    """Return the component number of each root: -1 for a texture root.

    The other roots (empty slots included) are numbered from 0 in root
    order. Does not walk anything.
    """
    numbers, count = [], 0
    for root in model.roots:
        if root is not None and model.nodes[root].type == TEXTURE:
            numbers.append(TEXTURE_ROOTS)
        else:
            numbers.append(count)
            count += 1
    return numbers


def draw_pass(
    model: OptModel,
    lod: int,
    switch: int,
    lists: dict[int, tuple[FaceRef, ...]] | None = None,
    resolver: Resolver | None = None,
) -> DrawPass:
    """Return what one pass draws: each component's faces, in drawing order.

    ``lod`` picks each face group's child ``lod`` (1 for the first; none
    when it has fewer), ``switch`` each node switch's child ``switch + 1``
    (its last when it has fewer). ``lists`` is ``regroup(model)`` (worked
    out when not given). The components run 0 to C - 1, preceded by
    component -1 when a texture root drew faces. Does not sort the faces.
    """
    resolver = resolver or Resolver(model)
    lists = lists if lists is not None else regroup(model, resolver)
    walk = _Walk(model, lod, switch, lists, resolver)
    state = _State()
    drawn: dict[int, list[DrawnFace]] = {}
    for root, number in zip(model.roots, components(model), strict=True):
        walk.faces = drawn.setdefault(number, [])
        if root is not None:
            walk.walk(root, state)
    result = [
        DrawnComponent(n, tuple(drawn[n]))
        for n in sorted(drawn)
        if n != TEXTURE_ROOTS or drawn[n]
    ]
    logger.debug(
        "pass lod %d switch %d: %d faces",
        lod,
        switch,
        sum(len(c.faces) for c in result),
    )
    return DrawPass(lod, switch, tuple(result), walk.meshes, walk.missing)


def passes(model: OptModel) -> list[tuple[int, int]]:
    """Return the sheets' passes: every level with switch 0, then every switch.

    ``(1, 0)`` to ``(L, 0)``, then ``(1, 1)`` to ``(1, S - 1)``, with ``L``
    and ``S`` the most children of any face group and node switch. Does
    not walk anything.
    """
    lods = most_children(model, FACE_GROUP)
    switches = most_children(model, NODE_SWITCH)
    return [(lod, 0) for lod in range(1, lods + 1)] + [
        (1, switch) for switch in range(1, switches)
    ]


def draw_model(model: OptModel) -> list[DrawPass]:
    """Return every pass of ``passes(model)``, the rewrite done once.

    Does not sort the faces within a component.
    """
    resolver = Resolver(model)
    lists = regroup(model, resolver)
    return [draw_pass(model, lod, sw, lists, resolver) for lod, sw in passes(model)]

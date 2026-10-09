"""Print the models' views in the answer sheets' form: file, draw, objects.

Purpose:
    ``render_file`` prints every model of a view as read (the ``file``
    sheet), ``render_draw`` every face the drawing walk draws in each pass
    (the ``draw`` sheet), and ``render_objects`` the object and craft type
    tables and the spec lists (the ``objects`` sheet), so a rendering diffs
    empty against its sheet; ``model_lines`` prints one model, for the
    command line's ``models dump``.

Flow:
    ``model_lines`` -> the model line, its roots, then each node and its
    payload line (``_payload_line``); ``draw.draw_lines`` prints the
    passes; ``objects`` prints the tables. Every sheet ends with one
    newline.

Invariants:
    - Floats print with C's ``%.9g`` (``number``); hashes as 16 lowercase
      hex digits of ``fnv1a64``; quoted names escape ``"`` and ``\\`` and
      print any byte outside printable ASCII as ``\\xHH`` (``quote``).
    - A name the view lacks prints ``model "NAME" missing``; one the
      reader refused, ``model "NAME" file="PATH" not_loaded``.

Call:
    ``render_file(models_view(install))``
"""

from __future__ import annotations

import logging
import math
import struct
from collections.abc import Iterable

from ..pictures.game import fnv1a64
from .colors import glow_of
from .draw import components
from .draw import Corner
from .draw import draw_model
from .draw import DrawnFace
from .draw import DrawPass
from .draw import most_children
from .game import LOADED
from .game import MISSING
from .game import ModelEntry
from .game import ModelsView
from .model import face_data
from .model import FACE_GROUP
from .model import FACE_TYPES
from .model import floats
from .model import HARDPOINT
from .model import MATERIALS
from .model import MESH_DESCRIPTOR
from .model import NAME_REFERENCE
from .model import NODE_SWITCH
from .model import OptModel
from .model import OptNode
from .model import OptTexture
from .model import ROTATION
from .model import ROTATION_SCALE
from .model import TEXTURE
from .model import TEXTURE_COORDINATES
from .model import TRANSFORM
from .model import VERTEX_NORMALS
from .model import VERTICES
from .objects import ObjectsView
from .tables import CRAFT_OBJECTS
from .tables import OBJECT_TYPES

logger = logging.getLogger(__name__)

VALUE_TYPES = (TRANSFORM, ROTATION, ROTATION_SCALE, 4, 6, 19)
"""The types whose payload prints as ``values v=`` and its floats."""
FULL_BLOCK = 16


def number(value: float) -> str:
    """Return a float as C's ``%.9g`` prints it, ``-0`` and ``-nan`` included.

    Always returns text. Does not round to the 4-byte float: give it the
    value read.
    """
    if math.isnan(value):
        return "-nan" if math.copysign(1.0, value) < 0 else "nan"
    return f"{value:.9g}"


def numbers(values: Iterable[float]) -> str:
    """Return floats printed by ``number``, joined by commas; no count check."""
    return ",".join(number(v) for v in values)


def hex64(data: bytes) -> str:
    """Return ``fnv1a64`` of ``data`` as 16 lowercase hex digits; no other form."""
    return f"{fnv1a64(data):016x}"


def quote(value: bytes | str) -> str:
    """Return a name quoted as the model sheets print it.

    ``"`` and ``\\`` take a backslash; a byte outside 32 to 126 prints as
    ``\\x`` and two lowercase hex digits. A ``str`` stands for its Latin-1
    bytes. Does not stop at a 0 byte (names never hold one).
    """
    data = value.encode("latin-1") if isinstance(value, str) else value
    out = []
    for byte in data:
        if byte in (0x22, 0x5C):
            out.append("\\" + chr(byte))
        elif 32 <= byte <= 126:
            out.append(chr(byte))
        else:
            out.append(f"\\x{byte:02x}")
    return '"' + "".join(out) + '"'


def name_text(name: bytes | None) -> str:
    """Return a node name quoted by ``quote``, or ``-`` for none; no length check."""
    return "-" if name is None else quote(name)


def _children(node: OptNode) -> str:
    return ",".join("-" if c is None else str(c) for c in node.children)


def _palette_fields(texture: OptTexture) -> str:
    if texture.sub_palettes != FULL_BLOCK:
        return f"block={hex64(texture.palette)}"
    glow = glow_of(texture)
    glow_bytes = struct.pack("<256H", *glow.colors)
    colors = " ".join(
        f"colors{k}={hex64(texture.color_bytes(k))}" for k in range(FULL_BLOCK)
    )
    return (
        f"shade={hex64(texture.shading)} {colors} glow_count={glow.count} "
        f"glow_first={glow.first} glow={hex64(glow_bytes)}"
    )


def texture_line(node: OptNode) -> str:
    """Return a texture node's payload line: header, hashes, palette.

    ``palette=`` is the carrier's node number, ``inline`` or ``none``.
    Raises ``ValueError`` for a node without a texture. Does not check the
    texture's fields.
    """
    texture = node.texture
    if texture is None:
        raise ValueError(f"node {node.number} holds no texture")
    if texture.palette_node is not None:
        carrier = str(texture.palette_node)
    else:
        carrier = "inline" if texture.inline_palettes else "none"
    return (
        f"  texture width={texture.width} height={texture.height} "
        f"texture_size={texture.texture_size} data_size={texture.data_size} "
        f"inline_palettes={texture.inline_palettes} top={hex64(texture.top)} "
        f"texels={hex64(texture.texels)} palette={carrier} {_palette_fields(texture)}"
    )


def _faces_line(node: OptNode, version: int) -> str:
    data = face_data(node, version)
    return (
        f"  faces edges={data.edge_count} records={hex64(data.records)} "
        f"normals={hex64(data.normals)} gradients={hex64(data.gradients)} "
        f"extra_normals={node.extra_normals} extra={hex64(data.extra)}"
    )


def _descriptor_line(node: OptNode) -> str:
    mesh_type, flags = struct.unpack_from("<2i", node.payload)
    values = struct.unpack_from("<12f", node.payload, 8)
    target_id = struct.unpack_from("<i", node.payload, 56)[0]
    target = struct.unpack_from("<3f", node.payload, 60)
    return (
        f"  descriptor mesh_type={mesh_type} flags={flags} "
        f"span={numbers(values[0:3])} center={numbers(values[3:6])} "
        f"min={numbers(values[6:9])} max={numbers(values[9:12])} "
        f"target_id={target_id} target={numbers(target)}"
    )


def _payload_line(node: OptNode, version: int) -> str | None:
    """Return a node's payload line, or None for a type that prints none."""
    kind = node.type
    if kind in FACE_TYPES:
        return _faces_line(node, version)
    if kind in (VERTICES, VERTEX_NORMALS):
        return f"  vectors hash={hex64(node.payload)}"
    if kind == TEXTURE_COORDINATES:
        return f"  coords hash={hex64(node.payload)}"
    if kind == MATERIALS:
        return f"  materials hash={hex64(node.payload)}"
    if kind == FACE_GROUP:
        return f"  levels distances={numbers(floats(node))}"
    if kind == HARDPOINT:
        point_type = struct.unpack_from("<i", node.payload)[0]
        at = struct.unpack_from("<3f", node.payload, 4)
        return f"  hardpoint type={point_type} at={numbers(at)}"
    if kind == MESH_DESCRIPTOR:
        return _descriptor_line(node)
    if kind in VALUE_TYPES:
        return f"  values v={numbers(floats(node))}"
    if kind == NAME_REFERENCE:
        return f"  ref {quote(node.reference or b'')}"
    if kind == TEXTURE:
        return texture_line(node)
    return None


def node_lines(node: OptNode, version: int) -> list[str]:
    """Return a node's line and, for a type with one, its payload line.

    Does not print the node's children's lines.
    """
    lines = [
        f"node {node.number} type={node.type} name={name_text(node.name)} "
        f"children={_children(node)} payload_count={node.count}"
    ]
    payload = _payload_line(node, version)
    if payload is not None:
        lines.append(payload)
    return lines


def file_body_lines(model: OptModel) -> list[str]:
    """Return a model's root lines and node lines, in node order.

    Does not print the model line.
    """
    lines = [
        f"root {i} node={'-' if root is None else root}"
        for i, root in enumerate(model.roots)
    ]
    for node in model.nodes:
        lines += node_lines(node, model.version)
    return lines


def entry_head(entry: ModelEntry) -> str:
    """Return a model line's opening, or the whole line of a model not read.

    ``model "NAME" file="PATH"`` for a loaded model; ``model "NAME"
    missing`` for a name the view lacks; ``model "NAME" file="PATH"
    not_loaded`` for a file the reader refused. Does not look at the model.
    """
    name = quote(entry.name)
    if entry.status == MISSING:
        return f"model {name} missing"
    head = f"model {name} file={quote(entry.file or '')}"
    return head if entry.status == LOADED else f"{head} not_loaded"


def model_lines(entry: ModelEntry) -> list[str]:
    """Return one model's lines of the ``file`` sheet.

    One line for a missing or refused model; else the model line, its
    roots and its nodes. Does not draw the model.
    """
    head = entry_head(entry)
    model = entry.model
    if model is None:
        return [head]
    return [
        f"{head} version={model.version} roots={len(model.roots)} "
        f"nodes={len(model.nodes)} reserved={model.reserved}",
        *file_body_lines(model),
    ]


def render_file(view: ModelsView) -> str:
    """Return the ``file`` sheet: every name of the view, in the list's order.

    Returns a single newline for an empty list. Does not regroup or draw.
    """
    lines: list[str] = []
    for entry in view.models:
        lines += model_lines(entry)
    logger.debug("file sheet: %d lines", len(lines))
    return "\n".join(lines) + "\n"


def _corner_values(values: list[tuple[float, ...] | None]) -> str:
    return ";".join("bad" if v is None else numbers(v) for v in values)


def face_line(face: DrawnFace, model: OptModel) -> str:
    """Return one drawn face's sheet line.

    ``tex`` is the texture's name as it is (Latin-1, not escaped), ``-``
    for a texture with no name, ``white`` for the built-in white texture.
    Does not check the corner count.
    """
    if face.texture is None:
        tex = "white"
    else:
        name = model.nodes[face.texture].name
        tex = "-" if name is None else '"' + name.decode("latin-1") + '"'
    corners: tuple[Corner, ...] = face.corners
    return (
        f"face tex={tex} corners={len(corners)} "
        f"p={_corner_values([c.position for c in corners])} "
        f"uv={_corner_values([c.uv for c in corners])} "
        f"n={_corner_values([c.normal for c in corners])} fn={numbers(face.normal)}"
    )


def pass_lines(drawn: DrawPass, model: OptModel) -> list[str]:
    """Return one pass's lines: the pass line, then each component's faces.

    Faces are sorted by their lines' bytes within each component. The
    pass line's ``meshes`` counts the face data nodes that drew a face;
    ``walk=unchecked`` says the faces were not checked against the
    game's own drawing. Does not print the model line.
    """
    total = sum(len(c.faces) for c in drawn.components)
    lines = [
        f"pass lod={drawn.lod} switch={drawn.switch} meshes={drawn.meshes} "
        f"faces={total} walk=unchecked"
    ]
    for component in drawn.components:
        lines.append(f"component {component.number} faces={len(component.faces)}")
        faces = [face_line(face, model) for face in component.faces]
        lines += sorted(faces, key=lambda text: text.encode("latin-1"))
    return lines


def draw_lines(entry: ModelEntry) -> list[str]:
    """Return one model's lines of the ``draw`` sheet.

    One line for a missing or refused model; else the model line and
    every pass of ``draw.passes``. Does not print the reader's view.
    """
    head = entry_head(entry)
    model = entry.model
    if model is None:
        return [head]
    count = sum(1 for n in components(model) if n >= 0)
    lines = [
        f"{head} version={model.version} roots={len(model.roots)} "
        f"components={count} lods={most_children(model, FACE_GROUP)} "
        f"switches={most_children(model, NODE_SWITCH)}"
    ]
    for drawn in draw_model(model):
        lines += pass_lines(drawn, model)
    return lines


def render_draw(view: ModelsView) -> str:
    """Return the ``draw`` sheet: every name of the view, in the list's order.

    Returns a single newline for an empty list. Does not print the
    reader's view of the nodes.
    """
    lines: list[str] = []
    for entry in view.models:
        lines += draw_lines(entry)
    logger.debug("draw sheet: %d lines", len(lines))
    return "\n".join(lines) + "\n"


def object_lines() -> list[str]:
    """Return the ``object`` and ``craft`` lines of the two tables, in order.

    Flags print as ``0x`` and two lowercase hex digits. Does not look at
    any list.
    """
    lines = [
        f"object {t} record_flags=0x{o.record_flags:02x} "
        f"asset_flags=0x{o.asset_flags:02x} model_index={o.model_index} "
        f"texture_group={o.texture_group} resource_index={o.resource_index}"
        for t, o in enumerate(OBJECT_TYPES)
    ]
    lines += [f"craft {c} object={t}" for c, t in enumerate(CRAFT_OBJECTS)]
    return lines


def render_objects(view: ObjectsView) -> str:
    """Return the ``objects`` sheet: the two tables, then each list's lines.

    Lists in group order, 640 before 320; a list the view lacks prints
    ``missing`` and no entries. Does not resolve any model path.
    """
    lines = object_lines()
    for (group, size), spec in view.lists.items():
        head = f"list {group} {size} {quote(spec.game_path)}"
        if spec.file is None:
            lines.append(f"{head} missing")
            continue
        lines.append(f"{head} file={quote(spec.file)}")
        lines += [
            f"entry {group} {size} {i} {quote(line)}"
            for i, line in enumerate(spec.lines)
        ]
    logger.debug("objects sheet: %d lines", len(lines))
    return "\n".join(lines) + "\n"

"""A texture node's payload: its header, its texels, and the palette it uses.

Purpose:
    Read a texture the way the game reads it: the 24-byte header, the
    texel bytes (all mip levels when the texture size is the top level's,
    else the top level alone), and its palette: inline after the texels,
    a 16-sub-palette block it carries after its texels, or the block
    another texture of the same file carries.

Flow:
    ``read_texture`` reads one texture as the reader reaches it; a texture
    whose palette link names another texture's block is left with an empty
    palette until ``link_palettes`` has seen every node and fills it in.

Invariants:
    - The header, the texels and any palette a texture carries lie inside
      the body, or the model is refused; nothing here pads with zeros.
    - A shared palette link must name the very start of a block that a
      texture of the same model carries, before or after it in the file.
    - ``palette_node`` is the carrier's node number (the texture's own
      when it carries its block), ``None`` for an inline palette.

Call:
    ``texture, payload = read_texture(body, offset, number)``
"""

from __future__ import annotations

import logging
import struct
from dataclasses import replace

from .body import Body
from .body import ModelFormatError
from .model import OptNode
from .model import OptTexture

logger = logging.getLogger(__name__)

HEADER_SIZE = 24
MAX_INLINE = 16
MAX_SIDE = 16384
SUB_PALETTE_BYTES = 768
BLOCK_SIZE = 16 * SUB_PALETTE_BYTES
PENDING = -1
"""``palette_node`` of a texture whose shared block is not yet found."""


def read_texture(body: Body, offset: int, number: int) -> tuple[OptTexture, bytes]:
    """Return a texture read at body ``offset`` and the payload bytes it keeps.

    The payload is the header, the texels and the palette the texture
    carries (inline or its own block). A texture sharing another's block
    comes back with an empty palette and ``palette_node`` ``PENDING``.
    Raises ``ModelFormatError`` for an inline count outside 0 to 16, a
    side outside 1 to 16,384, fewer texel bytes than ``width x height``,
    or a header, texels or carried palette outside the body. Does not
    check the texel values.
    """
    header = body.need(offset, HEADER_SIZE, f"node {number} texture header")
    link, inline, texture_size, data_size, width, height = struct.unpack("<I5i", header)
    if not 0 <= inline <= MAX_INLINE:
        raise ModelFormatError(f"node {number} inline palette count {inline}")
    if not (1 <= width <= MAX_SIDE and 1 <= height <= MAX_SIDE):
        raise ModelFormatError(f"node {number} texture size {width}x{height}")
    pixels = width * height
    count = data_size if texture_size == pixels else pixels
    if count < pixels:
        raise ModelFormatError(
            f"node {number} has {count} texel bytes, fewer than {width}x{height}"
        )
    start = offset + HEADER_SIZE
    texels = body.need(start, count, f"node {number} texels")
    end = start + count
    if inline:
        palette = body.need(end, inline * SUB_PALETTE_BYTES, f"node {number} palette")
        carrier: int | None = None
    elif link - body.base == end:
        palette = body.need(end, BLOCK_SIZE, f"node {number} palette block")
        carrier = number
    else:
        palette, carrier = b"", PENDING
    logger.debug(
        "texture node %d: %dx%d, %d texel bytes, palette %s",
        number,
        width,
        height,
        count,
        "inline" if carrier is None else ("shared" if carrier == PENDING else "own"),
    )
    texture = OptTexture(
        link, inline, texture_size, data_size, width, height, texels, palette, carrier
    )
    return texture, header + texels + palette


def link_palettes(body: Body, nodes: list[OptNode]) -> list[OptNode]:
    """Return the nodes with every shared palette filled in from its carrier.

    A texture whose palette link names the start of a block another
    texture carries gets that block and the carrier's node number; the
    first carrier by node number wins when two share an address. Raises
    ``ModelFormatError`` when no texture carries a block there. Does not
    change textures that carry their own palette.
    """
    carriers: dict[int, OptNode] = {}
    for node in nodes:
        texture = node.texture
        if texture is not None and texture.palette_node == node.number:
            carriers.setdefault(texture.palette_link - body.base, node)
    result = []
    for node in nodes:
        texture = node.texture
        if texture is None or texture.palette_node != PENDING:
            result.append(node)
            continue
        carrier = carriers.get(texture.palette_link - body.base)
        if carrier is None or carrier.texture is None:
            raise ModelFormatError(
                f"node {node.number} palette link {texture.palette_link:#x} "
                "names no block a texture carries"
            )
        shared = replace(
            texture, palette=carrier.texture.palette, palette_node=carrier.number
        )
        logger.debug("texture node %d shares node %d's", node.number, carrier.number)
        result.append(replace(node, texture=shared))
    return result

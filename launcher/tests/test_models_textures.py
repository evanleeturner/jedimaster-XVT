"""The textures: texel bytes, palettes (own, shared, inline), colors and glow.

Purpose:
    Prove a texture's texel bytes follow the texture size rule, its
    palette is its own block, another texture's (before or after it) or
    inline, an unowned link and anything outside the body are refused;
    565 colors widen to 8 bits as the game's card path does, and the glow
    rule decides each index by its three steps, with no glow when no
    index or every index glows.

Flow:
    Build synthetic textures with ``optbuilder``; read; check the
    texture, its colors and its glow.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_models_textures.py``
"""

from __future__ import annotations

import logging
import struct

import pytest
from optbuilder import build_opt
from optbuilder import N
from optbuilder import palette
from optbuilder import Tex

from jedimaster.models import glow_of
from jedimaster.models import ModelFormatError
from jedimaster.models import read_opt
from jedimaster.models import rgb8
from jedimaster.models import texture_rgb
from jedimaster.models.colors import base_sub_palette
from jedimaster.models.colors import NO_INDEX
from jedimaster.models.render import hex64
from jedimaster.models.render import texture_line

logger = logging.getLogger(__name__)

BLOCK = palette()


def tex_node(tex: Tex, name: bytes | None = b"Tex") -> N:
    """Return a texture node holding ``tex``."""
    return N(20, name, payload=tex)


def read_texture(tex: Tex, **kwargs):
    """Return the texture of a model holding only ``tex``."""
    return read_opt(build_opt([tex_node(tex)], **kwargs)).nodes[0].texture


def test_texels_with_levels_take_the_data_size():
    texels = bytes(range(8)) + bytes([200, 201, 202])
    texture = read_texture(Tex(4, 2, texels, texture_size=8, palette=BLOCK))
    assert texture.texels == texels and texture.data_size == 11
    assert texture.top == bytes(range(8))
    assert (texture.width, texture.height) == (4, 2)


def test_texels_without_levels_take_width_times_height():
    texels = bytes(range(8))
    tex = Tex(4, 2, texels, texture_size=999, data_size=11, palette=BLOCK)
    texture = read_texture(tex)
    assert texture.texels == texels and texture.texture_size == 999
    assert texture.palette == BLOCK


def test_fewer_texel_bytes_than_the_picture_refused():
    tex = Tex(4, 2, bytes(7), texture_size=8, data_size=7, palette=BLOCK)
    with pytest.raises(ModelFormatError, match="7 texel bytes, fewer than 4x2"):
        read_texture(tex)


@pytest.mark.parametrize(("width", "height"), ((0, 1), (1, 0), (16385, 1), (1, 16385)))
def test_texture_sides_refused(width, height):
    tex = Tex(width, height, bytes(4), texture_size=-1, palette=BLOCK)
    with pytest.raises(ModelFormatError, match="texture size"):
        read_texture(tex)


def test_largest_sides_accepted():
    texture = read_texture(Tex(16384, 1, bytes(16384), palette=BLOCK))
    assert texture.width == 16384
    texture = read_texture(Tex(1, 16384, bytes(16384), palette=BLOCK))
    assert texture.height == 16384


@pytest.mark.parametrize("inline", (17, -1))
def test_inline_palette_count_refused(inline):
    tex = Tex(2, 2, bytes(4), inline=inline, palette=palette(1))
    with pytest.raises(ModelFormatError, match="inline palette count"):
        read_texture(tex)


def test_own_palette():
    model = read_opt(build_opt([N(0), tex_node(Tex(2, 2, bytes(4), palette=BLOCK))]))
    texture = model.nodes[1].texture
    assert texture.palette_node == 1 and texture.palette == BLOCK
    assert texture.sub_palettes == 16
    assert len(model.nodes[1].payload) == 24 + 4 + 12288


def test_shared_palettes_earlier_and_later():
    other = palette(color=lambda sub, index: index)
    first = tex_node(Tex(2, 1, b"\1\2", palette=BLOCK), b"First")
    later = tex_node(Tex(2, 1, b"\3\4", palette=other), b"Later")
    uses_first = tex_node(Tex(1, 1, b"\5", shares=first), b"A")
    uses_later = tex_node(Tex(1, 1, b"\6", shares=later), b"B")
    model = read_opt(build_opt([first, uses_first, uses_later, later]))
    textures = [n.texture for n in model.nodes]
    assert [t.palette_node for t in textures] == [0, 0, 3, 3]
    assert textures[1].palette == BLOCK and textures[2].palette == other
    assert len(model.nodes[1].payload) == 24 + 1
    assert texture_line(model.nodes[2]).count("palette=3 ") == 1


def test_inline_palettes():
    full = palette(16, color=lambda sub, index: sub)
    model = read_opt(build_opt([tex_node(Tex(2, 1, b"\1\2", inline=16, palette=full))]))
    texture = model.nodes[0].texture
    assert texture.palette_node is None and texture.palette == full
    assert texture.colors(5) == [5] * 256
    line = texture_line(model.nodes[0])
    assert " palette=inline shade=" in line and "colors15=" in line
    two = palette(2)
    model = read_opt(build_opt([tex_node(Tex(2, 1, b"\1\2", inline=2, palette=two))]))
    line = texture_line(model.nodes[0])
    assert line.endswith(f" palette=inline block={hex64(two)}")
    assert model.nodes[0].texture.sub_palettes == 2


def test_an_unowned_palette_link_is_refused():
    carrier = tex_node(Tex(2, 1, b"\1\2", palette=BLOCK))
    stray = tex_node(Tex(1, 1, b"\5", palette_link=0x10000 + 20))
    with pytest.raises(ModelFormatError, match="names no block a texture carries"):
        read_opt(build_opt([carrier, stray]))
    zero = tex_node(Tex(1, 1, b"\5", palette_link=0))
    with pytest.raises(ModelFormatError, match="names no block"):
        read_opt(build_opt([zero]))


def test_palettes_and_texels_must_lie_in_the_body():
    with pytest.raises(ModelFormatError, match="palette block"):
        read_texture(Tex(2, 1, b"\1\2", palette=BLOCK), cut=1)
    tex = Tex(2, 1, b"\1\2", inline=1, palette=palette(1))
    with pytest.raises(ModelFormatError, match="palette"):
        read_texture(tex, cut=1)
    with pytest.raises(ModelFormatError, match="texels"):
        read_texture(Tex(4, 4, bytes(16)), cut=1)
    with pytest.raises(ModelFormatError, match="texture header"):
        read_opt(build_opt([N(20, payload=b"\0" * 20)]))


def test_565_colors_widen_to_8_bits():
    assert rgb8(0xFFFF) == (255, 255, 255)
    assert rgb8(0) == (0, 0, 0)
    assert rgb8(0b10000_100000_10000) == (132, 130, 132)
    assert rgb8(0b00001_000001_00010) == (8, 4, 16)


def test_top_level_through_a_sub_palette():
    colors = palette(color=lambda sub, index: (sub << 11) | index)
    texture = read_texture(Tex(2, 1, b"\3\x1f", palette=colors))
    assert texture_rgb(texture, 8) == bytes((*rgb8(0x4003), *rgb8(0x401F)))
    assert texture_rgb(texture, 0) == bytes((0, 0, 24, 0, 0, 255))
    assert base_sub_palette(texture) == 8
    with pytest.raises(IndexError):
        texture_rgb(texture, 16)
    two = read_texture(Tex(1, 1, b"\0", inline=3, palette=palette(3)))
    assert base_sub_palette(two) == 2


def five(red: int, green: int, blue: int) -> int:
    """Return a 565 color from three 5-bit components (green's top five bits)."""
    return (red << 11) | (green << 6) | blue


def glow_palette(table: dict[int, list[int]], sub_palettes: int = 16) -> bytes:
    """Return a palette whose index ``i`` has ``table[i][sub]`` (default bright)."""
    bright = [five(20, 20, 20)] * 16

    def color(sub: int, index: int) -> int:
        return table.get(index, bright)[sub]

    return palette(sub_palettes, color=color)


def test_each_step_of_the_glow_rule():
    same = [five(20, 20, 20)] * 10 + [0x1234] + [0] * 5
    table = {
        0: [0] * 16,
        1: same,
        2: [five(20, 20, 20)] * 3 + [five(24, 20, 20) + 1] + [five(20, 20, 20)] * 12,
        3: [five(20, 20, 20)] * 3 + [five(24, 20, 20)] + [five(20, 20, 20)] * 12,
        4: [five(5, 1, 1)] * 16,
        5: [five(4, 4, 0)] * 16,
        6: [five(20, 20, 20)] * 6 + [five(20, 25, 20)] + [five(20, 20, 20)] * 9,
        7: [five(20, 20, 20)] * 7 + [0] + [five(20, 20, 20)] * 8,
        8: [five(20, 20, 20), five(20, 20, 20) | 0x20] + [five(20, 20, 20)] * 14,
        9: [(20 << 11) | (40 << 5) | 20] + [(20 << 11) | (32 << 5) | 20] * 15,
    }
    texture = read_texture(Tex(1, 1, b"\0", palette=glow_palette(table)))
    glow = glow_of(texture)
    lit = [i for i in range(10) if glow.mask[i]]
    assert lit == [1, 3, 5, 7, 8, 9]
    assert glow.glows and glow.count == 4 and glow.first == 0
    assert glow.colors[1] == 0x1234 and glow.colors[0] == 0 and glow.colors[2] == 0


def test_no_index_or_every_index_glowing_gives_no_glow():
    dark = read_texture(Tex(1, 1, b"\0", palette=palette(color=lambda s, i: 0)))
    glow = glow_of(dark)
    assert (glow.glows, glow.count, glow.first) == (False, 0, 0)
    assert glow.colors == (0,) * 256
    every = read_texture(Tex(1, 1, b"\0", palette=glow_palette({})))
    glow = glow_of(every)
    assert (glow.glows, glow.count, glow.first) == (False, 0, NO_INDEX)
    assert glow.colors == (five(20, 20, 20),) * 256 and all(glow.mask)
    line = texture_line(
        read_opt(
            build_opt([tex_node(Tex(1, 1, b"\0", palette=glow_palette({})))])
        ).nodes[0]
    )
    glow_bytes = struct.pack("<256H", *glow.colors)
    assert line.endswith(f"glow_count=0 glow_first=65535 glow={hex64(glow_bytes)}")


def test_a_short_inline_palette_has_no_glow():
    texture = read_texture(Tex(1, 1, b"\0", inline=10, palette=glow_palette({}, 10)))
    glow = glow_of(texture)
    assert (glow.glows, glow.count, any(glow.mask)) == (False, 0, False)


def test_glow_fields_of_the_file_sheet():
    table = {0: [0] * 16, 2: [0] * 16}
    model = read_opt(
        build_opt([tex_node(Tex(1, 1, b"\0", palette=glow_palette(table)))])
    )
    glow = glow_of(model.nodes[0].texture)
    glow_bytes = struct.pack("<256H", *glow.colors)
    line = texture_line(model.nodes[0])
    assert line.endswith(f"glow_count=2 glow_first=0 glow={hex64(glow_bytes)}")
    assert f"colors10={hex64(model.nodes[0].texture.color_bytes(10))}" in line
    assert f"shade={hex64(bytes(4096))}" in line

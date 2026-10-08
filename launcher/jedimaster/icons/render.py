"""Print the icon view in the answer sheets' three forms, and dump one bitmap.

Purpose:
    ``render_sheets``, ``render_tables`` and ``render_draws`` print the view
    line for line as the ``sheets``, ``tables`` and ``draws`` answer sheets
    print it, header lines included, so a rendering diffs empty against its
    sheet. ``render_bmp`` prints the bitmap reader's view of one file for
    the command line's ``icons dump``.

Flow:
    Header (``kind``, ``name``, ``file``, and for the sheets and draws the
    list's ``result`` and ``count``), then the kind's lines.

Invariants:
    - Every rendering ends with one newline.
    - Hex values are lowercase, four digits for a 565 color, two for a
      palette index; a draw's pixel not written prints ``----``.
    - ``DRAW_IFFS`` are the IFFs the draws sheet covers, in its order; the
      craft types run 0 to the table's last.

Call:
    ``render_draws(icons_view(install))``
"""

from __future__ import annotations

import dataclasses
import logging

from ..lists.text import c_quote
from .bmp import BmpFile
from .game import draw_icon
from .game import IconDraw
from .game import IconView
from .game import LIST_GAME_PATH
from .tables import BOXES
from .tables import CRAFT_BOXES

logger = logging.getLogger(__name__)

DRAW_IFFS = (0, 1, 2, 3, 4, 5, 6, 7, 255)
UNWRITTEN = "----"


def _join(lines: list[str]) -> str:
    return "\n".join(lines) + "\n"


def _header(kind: str, view: IconView) -> list[str]:
    return [
        f"kind {kind}",
        f"name {c_quote(LIST_GAME_PATH)}",
        f"file {c_quote(view.list_file)}",
        f"result {view.images.result}",
        f"count {len(view.images.images)}",
    ]


def render_sheets(view: IconView) -> str:
    """Return the sheets sheet: header, then each image in table order.

    Each image prints its line; an uncompressed image read whole adds its
    256 colors and its rows, top first, two hex digits per palette index.
    An uncompressed image that could not be read prints its line only.
    Does not check the view against the install.
    """
    lines = _header("sheets", view)
    for i, image in enumerate(view.images.images):
        lines.append(
            f"image {i} name={c_quote(image.name)} width={image.width} "
            f"height={image.height} compressed={int(image.compressed)}"
        )
        sheet = view.sheets.get(image.name)
        if image.compressed or sheet is None:
            continue
        lines.append("colors " + " ".join(f"{c:04x}" for c in sheet.colors))
        width = sheet.bitmap.width
        for y in range(sheet.bitmap.height):
            lines.append(
                f"row {y} {sheet.bitmap.pixels[y * width : (y + 1) * width].hex()}"
            )
    return _join(lines)


def render_tables() -> str:
    """Return the tables sheet: the boxes, then each craft type's box.

    Always the same text: the tables are this package's data. Does not
    check the boxes against any sheet.
    """
    lines = ["kind tables", f"boxes {len(BOXES)}"]
    for i, box in enumerate(BOXES):
        lines.append(
            f"box {i} left={box.left} top={box.top} right={box.right} bottom={box.bottom}"
        )
    lines.append(f"crafts {len(CRAFT_BOXES)}")
    lines += [f"craft {t} icon={b}" for t, b in enumerate(CRAFT_BOXES)]
    return _join(lines)


def draw_lines(draw: IconDraw | None, iff: int, craft: int) -> list[str]:
    """Return one draw's sheet lines: count, bounding box, rows.

    ``written=0`` and nothing else for no icon (None) or an icon that writes
    nothing; else ``at``/``size`` of the smallest box holding every written
    pixel, then one ``row`` per line of it. Does not check the pixels.
    """
    pixels = draw.pixels if draw is not None else []
    lines = [f"draw iff={iff} craft={craft} written={len(pixels)}"]
    if not pixels:
        return lines
    xs = [p.dx for p in pixels]
    ys = [p.dy for p in pixels]
    left, top = min(xs), min(ys)
    width, height = max(xs) - left + 1, max(ys) - top + 1
    lines.append(f"at {left} {top} size {width} {height}")
    grid = [[UNWRITTEN] * width for _ in range(height)]
    for p in pixels:
        grid[p.dy - top][p.dx - left] = f"{p.color:04x}"
    lines += [f"row {top + r} " + " ".join(cells) for r, cells in enumerate(grid)]
    return lines


def render_draws(view: IconView) -> str:
    """Return the draws sheet: header, then every IFF of ``DRAW_IFFS`` by craft.

    The outer loop runs over ``DRAW_IFFS``, the inner over craft types 0 to
    105. Does not draw craft types without a table entry.
    """
    lines = _header("draws", view)
    for iff in DRAW_IFFS:
        for craft in range(len(CRAFT_BOXES)):
            lines += draw_lines(draw_icon(view, iff, craft), iff, craft)
    return _join(lines)


def render_bmp(bmp: BmpFile) -> str:
    """Return the bitmap reader's view of one file: one ``name value`` a line.

    Every header field and what the decoding met, booleans as 0 and 1; the
    palette and pixels are not printed. Does not check any value.
    """
    lines = []
    for field in dataclasses.fields(bmp):
        if field.name in ("palette", "pixels"):
            continue
        value = getattr(bmp, field.name)
        lines.append(f"{field.name} {int(value)}")
    return _join(lines)

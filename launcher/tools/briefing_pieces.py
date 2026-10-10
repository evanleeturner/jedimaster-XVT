"""Write the briefing map's contact sheet: every drawn piece, as draw-record lists.

Purpose:
    The pixel check's input. The page draws the same records as the engine
    (``compare_briefings.py``); what is left is whether each record becomes
    the same pixels. That depends on the piece drawn, not on the mission, so
    these lists draw every piece once, laid out on panels the size of the
    briefing map (360 by 240): every box of the six icon sheets plain and in
    each tint color the stock briefings use, every glyph of font 10 in every
    named color, every shape in every named color, and the edge cases (the
    panel's edges, a smaller clip cut on each side, one-pixel boxes). The
    engine replays a list with ``briefing_pixels ... pieces``; the page
    paints the same list.

Flow:
    1. Build each panel's records in the format ``briefing_dump`` prints them,
       panel coordinates.
    2. Write one list per panel into the output folder, one record a line.

Invariants:
    - Only names, numbers and byte values: nothing from the game is written.
    - The same arguments always write the same files.
    - Bytes 1 to 6 are the text color codes and 0 ends a string, so the
      glyph lines hold bytes 7 to 255; the rest is escaped as briefing_dump
      escapes it (``\\"``, ``\\\\``, ``\\xHH`` outside space to tilde).

Call:
    ``python tools/briefing_pieces.py --out DIR``
"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

logger = logging.getLogger("briefing_pieces")

PANEL_WIDTH = 360
PANEL_HEIGHT = 240
SHEETS = ("mapicon0", "mapicon1", "mapicon2", "mapicon3", "mapicon4", "greyicon")
BOXES = 70
CELL = 24
COLUMNS = PANEL_WIDTH // CELL
SHADES = tuple(f"shade {n}" for n in range(40))
COLORS = (
    *SHADES,
    *(f"code{n}" for n in range(1, 6)),
    "yellow",
    "white",
    "major",
    "minor",
)
TINTS = tuple(f"shade {n}" for n in range(1, 40, 2))
GLYPHS = bytes(range(7, 256))
GLYPHS_PER_LINE = 20
LINE_HEIGHT = 14
SHAPE_ROW = 9
SHAPE_ROWS = 25


def quote(text: bytes) -> str:
    """Escape text as briefing_dump prints a glyph run."""
    out = []
    for byte in text:
        if byte in (ord('"'), ord("\\")):
            out.append("\\" + chr(byte))
        elif 0x20 <= byte <= 0x7E:
            out.append(chr(byte))
        else:
            out.append(f"\\x{byte:02x}")
    return "".join(out)


def icon_panel(sheets: tuple[str, ...], tint: str | None) -> list[str]:
    """Every box of each sheet in a 15-column grid, one sheet after another."""
    records = []
    row = 0
    for sheet in sheets:
        for box in range(BOXES):
            x = (box % COLUMNS) * CELL + 2
            y = (row + box // COLUMNS) * CELL + 1
            if tint is None:
                records.append(f"icon {sheet} {box} {x} {y}")
            else:
                records.append(f"tint {sheet} {box} {x} {y} {tint}")
        row += -(-BOXES // COLUMNS)
    return records


def glyph_panel(color: str) -> list[str]:
    """Bytes 7 to 255 of font 10 in color, twenty a line."""
    records = []
    for line, start in enumerate(range(0, len(GLYPHS), GLYPHS_PER_LINE)):
        run = GLYPHS[start : start + GLYPHS_PER_LINE]
        records.append(f'text 4 {2 + line * LINE_HEIGHT} {color} "{quote(run)}"')
    return records


def shape_panel(colors: tuple[str, ...]) -> list[str]:
    """For each color a row: fill, edge, outline, a line across, a line down."""
    records = []
    for row, color in enumerate(colors):
        top = 2 + row * SHAPE_ROW
        bottom = top + 6
        records += [
            f"fill 2 {top} 31 {bottom} {color}",
            f"edge 36 {top} 65 {bottom} {color}",
            f"outline 70 {top} 99 {bottom} {color}",
            f"hline {top + 3} 104 143 {color}",
            f"vline 150 {top} {bottom} {color}",
        ]
    return records


def edge_panel() -> list[str]:
    """The panel's own edges, pieces past them, one-pixel boxes, a cut clip."""
    records = [
        "vline 0 0 239 white",
        "vline 359 0 239 white",
        "hline 0 0 359 major",
        "hline 239 0 359 minor",
        "fill -5 -5 10 10 shade 4",
        "fill 350 230 365 245 shade 12",
        "outline -3 100 6 109 code2",
        "edge 354 100 363 109 code3",
        "fill 40 40 40 40 white",
        "edge 44 40 44 40 yellow",
        "outline 48 40 48 40 code1",
        "fill 52 40 53 41 shade 20",
        "icon mapicon0 0 -5 60",
        "icon mapicon1 3 350 60",
        "tint mapicon2 5 170 -6 shade 9",
        "tint greyicon 7 170 230 shade 31",
        'text -3 140 white "Past the left edge"',
        'text 300 140 code4 "Past the right edge"',
        'text 120 -4 yellow "Above the top"',
        'text 120 234 shade 33 "Below the bottom"',
        "clip 100 60 259 179",
        "fill 90 50 269 189 shade 2",
        "icon mapicon3 10 92 100",
        "icon mapicon4 12 250 100",
        "tint mapicon0 14 170 53 shade 15",
        "tint mapicon1 16 170 172 shade 25",
        'text 80 120 code5 "Cut on the left side"',
        'text 200 140 white "Cut on the right side"',
        "hline 60 90 269 major",
        "vline 100 50 189 minor",
        "clip 0 0 359 239",
        'text 4 200 white "The clip set back to the panel"',
    ]
    return records


def panels() -> dict[str, list[str]]:
    """Every panel's name and records, in a fixed order."""
    result: dict[str, list[str]] = {}
    groups = [SHEETS[i : i + 2] for i in range(0, len(SHEETS), 2)]
    for number, sheets in enumerate(groups, 1):
        result[f"icons-plain-{number}"] = icon_panel(sheets, None)
        for tint in TINTS:
            shade = int(tint.split()[1])
            result[f"icons-shade{shade:02d}-{number}"] = icon_panel(sheets, tint)
    for color in COLORS:
        result[f"glyphs-{color.replace(' ', '')}"] = glyph_panel(color)
    for number, start in enumerate(range(0, len(COLORS), SHAPE_ROWS), 1):
        result[f"shapes-{number}"] = shape_panel(COLORS[start : start + SHAPE_ROWS])
    result["edges"] = edge_panel()
    return result


def main(argv: list[str] | None = None) -> int:
    """Write every panel's list into --out; return 0."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True, help="the folder the lists go to")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    written = panels()
    for name, records in written.items():
        (out / f"{name}.txt").write_text(
            "".join(f"{r}\n" for r in records), encoding="latin-1"
        )
        logger.debug("pieces.list_written name=%s records=%d", name, len(records))
    logger.info(
        "pieces.lists_written count=%d records=%d out=%s",
        len(written),
        sum(len(r) for r in written.values()),
        out,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

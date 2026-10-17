"""What lies under the briefing map: the saved picture and which background.

Purpose:
    Rebuild, as the game does, the 360 by 240 picture the setup screen
    starts every frame from (its background, the pad, the activity frame,
    the map's shading and the caption band), and choose the background a
    team's briefing sits on.

Flow:
    ``background_for`` applies the background rule to a mission's directory,
    a team and the flight groups; ``screen_picture`` lays the first three
    layers over the whole 640 by 480 screen; ``saved_picture`` crops the
    panel from that screen and lays the last two over it, in 5-6-5 pixels;
    ``widened_png`` turns pixels into an 8-bit PNG, each channel widened by
    repeating its top bits so that packing the PNG's colors gives the 5-6-5
    pixels back; ``sprite_png`` does so for one bitmap at its own size with
    index 0 clear.

Invariants:
    - The screen is 640 by 480; a layer sits at (0, 0) and only its part
      inside the screen counts.
    - The panel is the screen rectangle from (84, 96) to (443, 335); a
      layer sits at screen (0, 0) and only its part inside the panel counts.
    - The background keeps every pixel (index 0 included); the pad and the
      activity frame skip index 0; the map shades every pixel that is not
      index 0; the caption band shades every pixel in its rows.
    - A shade is the channel average: red and green ``(a + b) // 2``, blue
      ``a // 2 + b // 2``.
    - A layer smaller than the screen leaves the rest as it was.

Call:
    ``png = widened_png(saved_picture(background, pad, frame, shade))``
"""

from __future__ import annotations

import logging
from collections.abc import Sequence

from ..icons import BmpFile
from ..icons import color_565
from ..icons.png import rgba_png

logger = logging.getLogger(__name__)

SCREEN_WIDTH = 640
SCREEN_HEIGHT = 480
PANEL_LEFT = 84
PANEL_TOP = 96
PANEL_WIDTH = 360
PANEL_HEIGHT = 240
BAND_FIRST_ROW = 209
BAND_LAST_ROW = 236
"""The caption band's rows in the panel (screen rows 305 to 332)."""
BLUE_565 = 0x001F
TRANSPARENT = 0

BACKGROUND_FILES = {
    "rebel": "frontres\\PLAYERR.BMP",
    "imperial": "frontres\\PLAYERI.BMP",
    "neutral": "frontres\\PLAYER.BMP",
}
"""The background file of each kind, as a game path."""
NEUTRAL_DIRECTORIES = ("melee", "tournament")
SIDED_DIRECTORIES = ("combat", "battle")
IMPERIAL_TEAM = 0
IFF_KINDS = {0: "rebel", 1: "imperial"}
UNDER_PREFIX = "under-"
UNDER_SUFFIX = ".png"


def under_art_name(kind: str) -> str:
    """Return the art name of background ``kind``: ``under-rebel.png`` and so on.

    Does not check that ``kind`` is one of ``BACKGROUND_FILES``.
    """
    return f"{UNDER_PREFIX}{kind}{UNDER_SUFFIX}"


def background_for(
    directory: str | None, team: int, player_iffs: Sequence[int | None]
) -> str:
    """Return the background kind (``rebel``, ``imperial``, ``neutral``) of a team.

    ``directory`` is the mission type's name (``melee``, ``tournament``,
    ``combat``, ``battle``, ``training``, ``campaign``) or None when the
    folder is not one of them. Melee and tournament are neutral; combat and
    battle are imperial for team 0 and rebel for any other team; every
    other directory takes the first flight group whose player number is not
    0 (``player_iffs`` holds, in the file's order, that group's IFF, or None
    for a group with player number 0): IFF 0 rebel, IFF 1 imperial, any
    other IFF neutral. With no such group the answer is neutral (the game
    keeps an earlier screen's background there; the page cannot). Does not
    check the team number.
    """
    if directory in NEUTRAL_DIRECTORIES:
        kind = "neutral"
    elif directory in SIDED_DIRECTORIES:
        kind = "imperial" if team == IMPERIAL_TEAM else "rebel"
    else:
        kind = "neutral"
        for iff in player_iffs:
            if iff is not None:
                kind = IFF_KINDS.get(iff, "neutral")
                break
    logger.debug("background: directory=%s team=%s -> %s", directory, team, kind)
    return kind


def shade(under: int, over: int) -> int:
    """Return the average of two 5-6-5 pixels, channel by channel.

    Red and green are ``(a + b) // 2``; blue is ``a // 2 + b // 2`` (so two
    blues of 1 average to 0). Does not check that the inputs fit 16 bits.
    """
    red = ((under >> 11) + (over >> 11)) // 2
    green = (((under >> 5) & 63) + ((over >> 5) & 63)) // 2
    blue = (under & 31) // 2 + (over & 31) // 2
    return red << 11 | green << 5 | blue


def _covered(bitmap: BmpFile):
    """Yield (panel offset, palette index) for each bitmap pixel in the panel."""
    columns = min(PANEL_WIDTH, bitmap.width - PANEL_LEFT)
    if columns <= 0:
        return
    for row in range(min(PANEL_HEIGHT, bitmap.height - PANEL_TOP)):
        start = (PANEL_TOP + row) * bitmap.width + PANEL_LEFT
        for column, index in enumerate(bitmap.pixels[start : start + columns]):
            yield row * PANEL_WIDTH + column, index


def _lay(panel: list[int], bitmap: BmpFile, whole: bool, shaded: bool = False) -> None:
    """Lay ``bitmap``'s part inside the panel over ``panel``, in place.

    Every pixel when ``whole``, else every pixel but index 0; a ``shaded``
    layer averages with the pixel under it (``shade``) instead of covering it.
    """
    colors = [color_565(*entry) for entry in bitmap.palette]
    for at, index in _covered(bitmap):
        if index == TRANSPARENT and not whole:
            continue
        panel[at] = shade(panel[at], colors[index]) if shaded else colors[index]


def screen_picture(
    background: BmpFile | None, pad: BmpFile | None, frame: BmpFile | None
) -> list[int]:
    """Return the screen's 307,200 5-6-5 pixels, rows top first, under the map.

    Lays, in order, ``background`` (every pixel), then ``pad`` and ``frame``
    (skipping index 0), each at (0, 0) and clipped to the screen. A layer
    given as None is left out. A layer smaller than the screen leaves the
    rest as it was. Does not check the layers' sizes.
    """
    screen = [0] * (SCREEN_WIDTH * SCREEN_HEIGHT)
    for layer, whole in ((background, True), (pad, False), (frame, False)):
        if layer is None:
            continue
        colors = [color_565(*entry) for entry in layer.palette]
        columns = min(SCREEN_WIDTH, layer.width)
        for row in range(min(SCREEN_HEIGHT, layer.height)):
            start = row * layer.width
            base = row * SCREEN_WIDTH
            for column, index in enumerate(layer.pixels[start : start + columns]):
                if index != TRANSPARENT or whole:
                    screen[base + column] = colors[index]
    logger.debug("screen picture laid: %d pixels", len(screen))
    return screen


def saved_picture(
    background: BmpFile | None,
    pad: BmpFile | None,
    frame: BmpFile | None,
    shading: BmpFile | None,
) -> list[int]:
    """Return the panel's 86,400 5-6-5 pixels, rows top first, of the saved picture.

    Lays, in order, ``background`` (every pixel), ``pad`` and ``frame``
    (skipping index 0), ``shading`` (shading every pixel that is not index
    0) and then shades the caption band with pure blue. A layer given as
    None is left out (the panel stays as it was). Does not check the
    layers' sizes: the part inside the panel is used.
    """
    screen = screen_picture(background, pad, frame)
    panel: list[int] = []
    for row in range(PANEL_HEIGHT):
        start = (PANEL_TOP + row) * SCREEN_WIDTH + PANEL_LEFT
        panel += screen[start : start + PANEL_WIDTH]
    if shading is not None:
        _lay(panel, shading, whole=False, shaded=True)
    for row in range(BAND_FIRST_ROW, BAND_LAST_ROW + 1):
        for column in range(PANEL_WIDTH):
            at = row * PANEL_WIDTH + column
            panel[at] = shade(panel[at], BLUE_565)
    logger.debug("saved picture laid: %d pixels", len(panel))
    return panel


def widen5(value: int) -> int:
    """Return a 5-bit channel as 8 bits by repeating its top bits."""
    return value << 3 | value >> 2


def widen6(value: int) -> int:
    """Return a 6-bit channel as 8 bits by repeating its top bits."""
    return value << 2 | value >> 4


def widened_png(
    panel: Sequence[int],
    width: int = PANEL_WIDTH,
    height: int = PANEL_HEIGHT,
    clear: Sequence[bool] | None = None,
) -> bytes:
    """Return ``panel`` (360 by 240 unless sized) as an 8-bit PNG.

    Each 5-6-5 pixel's channels are widened (``widen5``, ``widen6``), so
    packing the PNG's colors to 5-6-5 gives the pixel back. The picture is
    opaque, except that a pixel whose entry in ``clear`` is True gets alpha
    0. Raises ``ValueError`` when ``panel`` (or ``clear``) is not ``width``
    times ``height`` pixels.
    """
    if len(panel) != width * height:
        raise ValueError(f"{len(panel)} pixels are not a {width}x{height} panel")
    if clear is not None and len(clear) != len(panel):
        raise ValueError(f"{len(clear)} clear marks for {len(panel)} pixels")
    rgba = bytearray()
    for at, pixel in enumerate(panel):
        alpha = 0 if clear is not None and clear[at] else 255
        rgba += bytes(
            (widen5(pixel >> 11), widen6((pixel >> 5) & 63), widen5(pixel & 31), alpha)
        )
    return rgba_png(width, height, bytes(rgba))


def sprite_png(bitmap: BmpFile) -> bytes:
    """Return ``bitmap`` as an 8-bit PNG at its own size, index 0 clear.

    Every other pixel is its palette entry through the same 5-6-5 packing
    and widening the frame uses; index 0 gets alpha 0. Raises ``ValueError``
    for a bitmap with no pixels. Does not check the bitmap's size.
    """
    colors = [color_565(*entry) for entry in bitmap.palette]
    pixels = [colors[index] for index in bitmap.pixels]
    clear = [index == TRANSPARENT for index in bitmap.pixels]
    return widened_png(pixels, bitmap.width, bitmap.height, clear)

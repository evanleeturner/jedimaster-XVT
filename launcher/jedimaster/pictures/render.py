"""Print the pictures' view in the answer sheets' form, one line per name.

Purpose:
    ``render_pictures`` prints every name of a view as the pictures sheet
    does, so a rendering diffs empty against its sheet; ``picture_line``
    prints one, for the command line's ``pictures dump``.

Flow:
    One line per picture, in the list's order: loaded, missing or not
    loaded. The pictures sheet has no header lines.

Invariants:
    - A loaded picture prints its file label, size and the two hashes as
      16 lowercase hex digits.
    - Every rendering ends with one newline.

Call:
    ``render_pictures(pictures_view(install))``
"""

from __future__ import annotations

import logging

from ..text.sheet import join
from ..text.sheet import quote
from .game import colors_hash
from .game import LOADED
from .game import MISSING
from .game import Picture
from .game import PicturesView
from .game import pixels_hash

logger = logging.getLogger(__name__)


def picture_line(picture: Picture) -> str:
    """Return one picture's sheet line.

    ``picture "<name>" missing`` or ``... not_loaded`` without a bitmap;
    else its file, width, height and the two hashes. Does not check the
    hashes' inputs.
    """
    name = quote(picture.name)
    if picture.status != LOADED or picture.bitmap is None:
        word = picture.status if picture.status != LOADED else MISSING
        return f"picture {name} {word}"
    bitmap = picture.bitmap
    return (
        f"picture {name} file={quote(picture.file or '')} width={bitmap.width} "
        f"height={bitmap.height} pixels={pixels_hash(bitmap):016x} "
        f"colors={colors_hash(picture.colors):016x}"
    )


def render_pictures(view: PicturesView) -> str:
    """Return the pictures sheet: one line per name, in the list's order.

    Returns a single newline for an empty list. Does not check the view
    against the install.
    """
    if not view.pictures:
        return "\n"
    return join([picture_line(picture) for picture in view.pictures])

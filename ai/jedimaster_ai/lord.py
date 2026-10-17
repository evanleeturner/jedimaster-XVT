"""Lord: the rules a Sith Lord flies by and no other rank does.

Purpose:
    A craft at the Sith Lord's skill level (``tactics.SITH_LORD_LEVEL``, 7)
    flies as every fighting craft does, at its own skill tier, but it never
    retreats and it fires warheads unguided at any craft:

    - it fights every enemy after it, whatever its hull
      (``behaviors.attack.fights_back``): AI rulings 33 and 34, which send a
      craft below three quarters of its hull away evading, do not hold for it
    - its group's abort trigger never sends it home or stops its drop
      (``home_books``); its departure time and its orders done still do, so
      a mission's goals score as they did
    - it fires a warhead unguided at a small craft, a player's too, as well
      as at a freighter, starship or platform (``warheads.fires_unguided``,
      missiles idea E)

Invariants:
    - The rank is the view's alone (``CraftView.skill_level``, the level the
      engine promoted the craft to), so every machine agrees.
    - Pure: each answer depends only on the view and the books given. The
      abort book a Lord sets aside is logged at DEBUG each time.
"""

from __future__ import annotations

import logging

from . import tactics
from .world import Books
from .world import CraftView

logger = logging.getLogger(__name__)


def is_lord(view: CraftView) -> bool:
    """Return whether the craft flies at the Sith Lord's skill level."""
    return view.skill_level == tactics.SITH_LORD_LEVEL


def home_books(view: CraftView, books: int) -> int:
    """Return those of the books that send the craft home: all of them, but
    the abort book for a Sith Lord, which never retreats."""
    if not is_lord(view) or not books & Books.ABORTED:
        return books
    logger.debug(
        "lord.stays object=%d books=%d",
        view.object_index,
        books,
    )
    return books & ~Books.ABORTED

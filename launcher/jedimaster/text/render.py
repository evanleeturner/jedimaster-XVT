"""Print each text file's reading in the answer sheets' form, line for line.

Purpose:
    One renderer per file kind (``strings``, ``front``, ``specs``,
    ``errors``, ``joystick``, ``credits``), printing the reader's result
    exactly as the answer sheets do, header lines included, so a rendering
    diffs empty against its sheet.

Flow:
    ``render_text(kind, result, name, file)`` prints the three header lines
    (``sheet.header``) and the kind's lines; each ``_<kind>_lines`` builds
    one kind's.

Invariants:
    - Every rendering ends with one newline; values are quoted with
      ``sheet.quote``, each up to its field's size where the game's field
      has one.
    - A ``strings.txt`` the game refused (no result) prints its header only.
    - The readers' ``result`` is 1 for a file read: the game's readers
      return 1 when they read their file.

Call:
    ``render_text("front", read_front(path), "fronttxt.txt", "fronttxt.txt")``
"""

from __future__ import annotations

import logging
from typing import Any

from .credits import CreditsFile
from .credits import CreditsPage
from .joystick import DESCRIPTION_ROOM
from .joystick import JoystickFile
from .joystick import NAME_ROOM
from .menus import ErrorMessages
from .menus import FrontText
from .sheet import header
from .sheet import join
from .sheet import quote
from .specs import FIELDS
from .specs import SpecsFile
from .strings import StringsFile
from .tables import GOAL
from .tables import MODELS

logger = logging.getLogger(__name__)

RESULT = 1
"""What the game's reader returns for a file it read."""
CREDITS_TEXT = 256
"""The size of a credits line's text, as the sheets print it."""
SHOWN_HEADER = (
    ("duration", "duration"),
    ("fade", "fade"),
    ("text_x", "text_x"),
    ("text_y", "text_y"),
    ("logo", "logo"),
    ("logo_x", "logo_x"),
    ("logo_y", "logo_y"),
)
"""The header numbers a credits page prints, in the sheets' order."""
UNREAD = "unread"


def _strings_lines(strings: StringsFile) -> list[str]:
    lines: list[str] = []
    for table in strings.tables:
        if not table.present:
            lines.append(f"table {table.name} absent")
            continue
        lines.append(f"table {table.name} {len(table.entries)}")
        for entry in table.entries:
            if table.kind == GOAL:
                lines.append(f"entry {entry.index}.{entry.variant} {quote(entry.text)}")
            elif table.kind == MODELS:
                lines.append(
                    f"entry {entry.index} gender={entry.gender} {quote(entry.text)}"
                )
            else:
                lines.append(f"entry {entry.index} {quote(entry.text)}")
    return lines


def _front_lines(front: FrontText) -> list[str]:
    lines = [f"count {len(front.entries)}"]
    lines += [f"entry {i} {quote(text)}" for i, text in enumerate(front.entries)]
    return lines


def _specs_lines(specs: SpecsFile) -> list[str]:
    lines = [f"result {RESULT}"]
    for i, entry in enumerate(specs.entries):
        values = " ".join(
            f"{name}={quote(getattr(entry, name), size)}" for name, size in FIELDS
        )
        lines.append(f"spec {i} {values}")
    return lines


def _errors_lines(errors: ErrorMessages) -> list[str]:
    lines = [f"entry {i} {quote(text)}" for i, text in enumerate(errors.messages)]
    lines.append(f"count {len(errors.messages)}")
    return lines


def _joystick_lines(joystick: JoystickFile) -> list[str]:
    lines = [f"result {RESULT}", f"count {len(joystick.actions)}"]
    for i, action in enumerate(joystick.actions):
        lines.append(
            f"action {i} code={action.code} name={quote(action.name, NAME_ROOM)} "
            f"description={quote(action.description, DESCRIPTION_ROOM)}"
        )
    return lines


def page_header(page: CreditsPage) -> str:
    """Return a credits page's header words, each with a leading space.

    `` header=unread`` when no number was read; else each shown number as
    ``name=value``, a number not read as ``name=unread``. Does not print the
    paragraph (its buffer is printed) or the two numbers not kept.
    """
    if not page.header:
        return " header=unread"
    words = []
    for shown, field in SHOWN_HEADER:
        value = page.value(field)
        words.append(f" {shown}={UNREAD if value is None else value}")
    return "".join(words)


def _credits_lines(credits: CreditsFile) -> list[str]:
    lines = []
    for page in credits.pages:
        lines.append(
            f"page {page.index} result={RESULT} buffer={page.buffer} "
            f"more={int(page.more)}{page_header(page)}"
        )
        lines += [
            f"line {i} color={line.color:04x} {quote(line.text, CREDITS_TEXT)}"
            for i, line in enumerate(page.lines)
        ]
    return lines


RENDERERS: dict[str, Any] = {
    "strings": _strings_lines,
    "front": _front_lines,
    "specs": _specs_lines,
    "errors": _errors_lines,
    "joystick": _joystick_lines,
    "credits": _credits_lines,
}
"""Each kind's line builder, in the sheets' order of kinds."""


def render_text(kind: str, result: Any, name: str, file: str) -> str:
    """Return one kind's sheet: the header lines, then the kind's lines.

    ``result`` is the kind's reader result (None for a refused
    ``strings.txt``, which prints its header only); ``name`` is the game
    name and ``file`` the file it resolved to. Raises ``KeyError`` for an
    unknown kind. Does not check that the result is of the kind.
    """
    lines = header(kind, name, file)
    if result is not None:
        lines += RENDERERS[kind](result)
    return join(lines)

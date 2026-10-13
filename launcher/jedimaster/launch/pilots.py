"""The pilots the game has: where it keeps their files and how to list them.

Purpose:
    Name the folder where the game keeps pilot files (``*.plt``) on this
    computer, and list the pilot names in it the way the game does, so the
    launcher can offer only pilots the game will accept.

Flow:
    ``default_pilot_folder`` names the game's user folder for a system.
    ``list_pilots`` reads the first 14 bytes of each ``*.plt`` file in a
    folder (the name, ended by a zero byte) and keeps the names the launch
    door accepts (``name_ok``). ``fit_name`` makes any text (a Discord
    name) into a name ``name_ok`` accepts, or gives up.

Invariants:
    - The folder is the one the engine opens: SDL's per-user data folder for
      the organisation ``TotallyOpen`` and the application ``OpenXvT``
      (``xvt_app/host_config.c``): ``$XDG_DATA_HOME`` or ``~/.local/share``
      on Linux, ``%APPDATA%`` on Windows, ``~/Library/Application Support``
      on macOS, each followed by ``TotallyOpen/OpenXvT``. Any other system
      has no default: the caller must be given the folder.
    - Only the folder's own ``*.plt`` files count, in any letter case of the
      extension, as the engine's listing does; a name is kept once, the
      first by file name wins.
    - A name must pass ``name_ok``, the rules of the door's name check
      (``launch_description.c``).

Call:
    ``list_pilots(default_pilot_folder("linux", {}))``
"""

from __future__ import annotations

import logging
import os
import re
import sys
import unicodedata
from pathlib import Path

logger = logging.getLogger(__name__)

ORGANISATION = "TotallyOpen"
APPLICATION = "OpenXvT"
NAME_BYTES = 14
NAME_LIMIT = 12
EXTRA_CHARACTERS = " _-."
SWITCH_WORDS = (
    "traincourse", "nopilot", "dinput", "sfx", "music", "voice", "tickcounter",
    "mipmaps", "inprogress", "newnet", "nolauncher", "fullscreen", "pageflip",
)  # fmt: skip
"""Words the engine reads as flight switches when they appear in a name."""


def default_pilot_folder(
    platform: str | None = None, env: dict[str, str] | None = None
) -> Path | None:
    """Return the game's user folder for ``platform`` (default: this one), or None.

    None for a system whose folder the engine's source does not settle (only
    Linux, macOS and Windows are settled). ``env`` stands in for the
    environment. Does not create or look at any folder.
    """
    platform = sys.platform if platform is None else platform
    env = dict(os.environ) if env is None else env
    home = Path.home()
    if platform.startswith("linux"):
        data = env.get("XDG_DATA_HOME")
        base = Path(data) if data else home / ".local" / "share"
    elif platform == "darwin":
        base = home / "Library" / "Application Support"
    elif platform.startswith("win"):
        appdata = env.get("APPDATA")
        base = Path(appdata) if appdata else home / "AppData" / "Roaming"
    else:
        logger.debug("no default pilot folder for %s", platform)
        return None
    return base / ORGANISATION / APPLICATION


def name_ok(name: str) -> bool:
    """Say whether the launch door accepts ``name`` as a pilot's name.

    One to 12 letters, digits, spaces, underscores, hyphens or dots, starting
    and ending with a letter or digit, holding none of the flight switch
    words in any letter case. Letters and digits are ASCII only.
    """
    if not 1 <= len(name) <= NAME_LIMIT:
        return False
    if not all(c.isascii() and (c.isalnum() or c in EXTRA_CHARACTERS) for c in name):
        return False
    if not (name[0].isalnum() and name[-1].isalnum()):
        return False
    lowered = name.lower()
    return not any(word in lowered for word in SWITCH_WORDS)


def _trimmed(text: str) -> str:
    start, end = 0, len(text)
    while start < end and not text[start].isalnum():
        start += 1
    while end > start and not text[end - 1].isalnum():
        end -= 1
    return text[start:end]


def fit_name(text: str) -> str:
    """Return ``text`` made into a name ``name_ok`` accepts, or "" when none is left.

    Letters lose their accents (``José`` becomes ``Jose``); every other
    character the rule does not allow is dropped; the flight switch words
    are taken out, again until none is left; runs of spaces become one; the
    ends are trimmed to a letter or digit; then the name is cut to 12 and
    its end trimmed again. Does not look at any file.
    """
    plain = unicodedata.normalize("NFKD", text)
    kept = "".join(
        c for c in plain if c.isascii() and (c.isalnum() or c in EXTRA_CHARACTERS)
    )
    previous = None
    while kept != previous:
        previous = kept
        for word in SWITCH_WORDS:
            kept = re.sub(re.escape(word), "", kept, flags=re.IGNORECASE)
    kept = _trimmed(re.sub(" +", " ", kept))
    kept = _trimmed(kept[:NAME_LIMIT])
    logger.debug("name %r fitted to %r", text, kept)
    return kept


def _name_in(path: Path) -> str | None:
    try:
        with path.open("rb") as stream:
            head = stream.read(NAME_BYTES)
    except OSError as exc:
        logger.warning("cannot read the pilot file %s: %s", path.name, exc)
        return None
    return head.split(b"\0", 1)[0].decode("latin-1")


def list_pilots(folder: Path | None) -> list[str] | None:
    """Return the pilot names in ``folder``, sorted by file name; None if unreadable.

    None when ``folder`` is None or cannot be listed. A file that cannot be
    read, or whose name fails ``name_ok``, is skipped (DEBUG). A name that
    repeats in another letter case is kept once. Does not check that the
    rest of a file is a pilot record.
    """
    if folder is None:
        return None
    try:
        files = sorted(
            (p for p in folder.iterdir() if p.suffix.lower() == ".plt" and p.is_file()),
            key=lambda p: p.name.lower(),
        )
    except OSError as exc:
        logger.debug("cannot list %s: %s", folder, exc)
        return None
    names: list[str] = []
    for path in files:
        name = _name_in(path)
        if name is None or not name_ok(name):
            logger.debug("pilot file %s skipped: %r", path.name, name)
            continue
        if name.lower() in (n.lower() for n in names):
            logger.debug("pilot %r repeats in %s", name, path.name)
            continue
        names.append(name)
    logger.debug("pilots in %s: %s", folder, names)
    return names

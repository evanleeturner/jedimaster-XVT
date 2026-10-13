"""The bot's pilot steps: the pilot made at the first link, shown, renamed once.

Purpose:
    Give every account that links the launcher a pilot at once, named from
    its Discord names, and let the player keep that name or change it once
    from Discord. ``Core`` takes these methods in, so the Link and Pilot
    commands and the buttons call the same decisions.

Flow:
    ``offer`` reads the asker's pilot or makes it (``first_name``) and says
    its name, with Keep name and Change name while the one change is left.
    ``pilot`` answers the Pilot command with the offer. ``keep_name``
    closes the offer; ``change_name`` opens the name window;
    ``rename_pilot`` takes the typed name.

Invariants:
    - Every step goes through ``_gate``: only the linked account, and only
      in a direct message.
    - A name is changed only when it passes ``name_ok`` as typed; a refused
      name leaves the one change unused and the buttons in place.
    - A pilot file that cannot be read is never made again over itself:
      the reply says so and nothing is written.

Call:
    ``reply = core.pilot(user_id, True, ("Mr. Webbin", "mr.webbin"))``
"""

from __future__ import annotations

import logging

from ..launch.pilot_store import first_name
from ..launch.pilot_store import Pilot
from ..launch.pilot_store import PilotFileError
from ..launch.pilots import NAME_LIMIT
from ..launch.pilots import name_ok
from .reply import Button
from .reply import Reply
from .reply import Window

logger = logging.getLogger(__name__)

KEEP = Button("Keep name", "pilot_keep", "")
CHANGE = Button("Change name", "pilot_change", "")
NAME_WINDOW_TITLE = "Pilot name"
NAME_WINDOW_LABEL = "Name, 1 to 12 characters"
OFFER = "Your pilot is {name}. Keep the name, or change it once."
SHOWN = "Your pilot is {name}."
KEPT = "Your pilot is {name}. The Pilot command can still change its name once."
RENAMED = "Your pilot is now {name}. The name cannot change again."
NO_CHANGE_LEFT = "Your pilot's name was changed once and cannot change again."
SAME_NAME = "That is your pilot's name already; nothing changed."
BAD_NAME = (
    "A pilot's name is 1 to 12 English letters, digits, spaces, _ - or ., "
    "starts and ends with a letter or digit, and holds none of the game's "
    "switch words (such as sfx, music or voice). Nothing changed."
)
NO_PILOT = "This account has no pilot yet. Use the Pilot command to make one."
BAD_FILE = "The launcher cannot read this account's pilot file; nothing changed."


class PilotWalk:
    """The pilot methods ``Core`` takes in; they use ``Core``'s own helpers."""

    def offer(self, user_id: int, names: tuple[str, str]) -> Reply:
        """Return the asker's pilot in words, making it first if there is none.

        ``names`` are the display name and the account name. Adds Keep name
        and Change name while the one change is left. Does not check who is
        asking: the caller has.
        """
        try:
            pilot = self.pilots.get(user_id)
            if pilot is None:
                display, account_name = names
                pilot = self.pilots.make(
                    user_id, first_name(display, account_name, user_id)
                )
        except PilotFileError as exc:
            logger.warning("pilot of account %d: %s", user_id, exc)
            return Reply(BAD_FILE)
        if pilot.renamed:
            return Reply(SHOWN.format(name=pilot.name))
        return Reply(OFFER.format(name=pilot.name), buttons=(KEEP, CHANGE))

    def pilot(self, user_id: int, in_dm: bool, names: tuple[str, str]) -> Reply:
        """Answer the Pilot command: the asker's pilot, made now if it has none.

        Returns ``offer``'s reply, or ``HOST_ONLY`` / ``DM_ONLY``.
        """
        self._begin("pilot", user_id)
        refusal = self._gate("pilot", user_id, in_dm, dm_only=True)
        if refusal is not None:
            return refusal
        reply = self.offer(user_id, names)
        self._end("pilot", user_id, "shown")
        return reply

    def keep_name(self, user_id: int, in_dm: bool) -> Reply:
        """Answer Keep name: close the offer, saying the change is still there.

        Edits the clicked message. Writes nothing.
        """
        self._begin("keep_name", user_id)
        refusal = self._gate("keep_name", user_id, in_dm, dm_only=True)
        if refusal is not None:
            return refusal
        pilot = self._own_pilot(user_id)
        if not isinstance(pilot, Pilot):
            return pilot
        words = SHOWN if pilot.renamed else KEPT
        self._end("keep_name", user_id, "kept")
        return Reply(words.format(name=pilot.name), edit=True)

    def change_name(self, user_id: int, in_dm: bool) -> Reply:
        """Answer Change name: open the name window, or say why not.

        The window starts with the pilot's name. Words alone when there is
        no pilot or its one change is used.
        """
        self._begin("change_name", user_id)
        refusal = self._gate("change_name", user_id, in_dm, dm_only=True)
        if refusal is not None:
            return refusal
        pilot = self._own_pilot(user_id)
        if not isinstance(pilot, Pilot):
            return pilot
        if pilot.renamed:
            self._end("change_name", user_id, "no change left")
            return Reply(NO_CHANGE_LEFT, edit=True)
        self._end("change_name", user_id, "window")
        return Reply(
            "",
            window=Window(
                "pilot_name",
                NAME_WINDOW_TITLE,
                NAME_WINDOW_LABEL,
                pilot.name,
                NAME_LIMIT,
            ),
        )

    def rename_pilot(self, user_id: int, typed: str, in_dm: bool) -> Reply:
        """Answer the name window: give the pilot the typed name, its one change.

        The typed text loses its outer spaces and must then pass ``name_ok``;
        a name that fails, or the pilot's own name, changes nothing and keeps
        the buttons. Edits the clicked message.
        """
        self._begin("rename_pilot", user_id)
        refusal = self._gate("rename_pilot", user_id, in_dm, dm_only=True)
        if refusal is not None:
            return refusal
        pilot = self._own_pilot(user_id)
        if not isinstance(pilot, Pilot):
            return pilot
        if pilot.renamed:
            self._end("rename_pilot", user_id, "no change left")
            return Reply(NO_CHANGE_LEFT, edit=True)
        name = typed.strip()
        if name == pilot.name:
            self._end("rename_pilot", user_id, "same name")
            return Reply(SAME_NAME, buttons=(KEEP, CHANGE), edit=True)
        if not name_ok(name):
            self._end("rename_pilot", user_id, "bad name")
            return Reply(BAD_NAME, buttons=(KEEP, CHANGE), edit=True)
        renamed = self.pilots.rename(user_id, name)
        self._end("rename_pilot", user_id, "renamed")
        return Reply(RENAMED.format(name=renamed.name), edit=True)

    def _own_pilot(self, user_id: int) -> Pilot | Reply:
        try:
            pilot = self.pilots.get(user_id)
        except PilotFileError as exc:
            logger.warning("pilot of account %d: %s", user_id, exc)
            return Reply(BAD_FILE, edit=True)
        if pilot is None:
            self._end("pilot", user_id, "no pilot")
            return Reply(NO_PILOT, edit=True)
        return pilot

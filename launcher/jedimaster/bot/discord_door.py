"""The one place the bot touches discord.py: the client, the commands, the menus.

Purpose:
    Carry ``core``'s decisions to Discord and back. Each row of the
    registry becomes a slash command and a right-click (user) command, both
    thin handlers that call the same ``Core`` method and send its reply
    privately; the menus and buttons ``core`` asks for become select menus
    and buttons; the client logs in with the bot's token and keeps the
    global command list in step; a battle's ending goes to the linked
    account by direct message.

Flow:
    ``Door`` builds a client with the guilds intent and a command tree of the
    registry's rows, twice each. A handler calls the matching ``Core`` method
    with the asker's account id (never the person right-clicked), sends the
    reply's pushes to the open pages (``deliver``), then answers with
    ``answer``, or opens the reply's window (``TextWindow``), whose typed
    text goes through ``Door.typed``. A pick or press goes through
    ``Door.picked`` / ``pressed``.
    ``on_push`` receives a battle's ending from the control. ``Door.run``
    logs in and runs until stopped; on start ``sync_if_changed`` sends the
    command list to Discord only when it differs from the last one sent.

Invariants:
    - This is the only module that imports discord.py.
    - Right-click items are user commands; the tree allows 15 of them (the
      limit in Discord's documentation, not discord.py's 5).
    - Every answer is ephemeral (visible to the asker only).
    - The Single player steps edit the message that was clicked (the walk
      stays in one message); the options menus go in two messages.
    - A row that reads files defers first; the others answer at once,
      inside Discord's three seconds.
    - The token is read from a ``Token`` only to log in and is never logged;
      neither is the link code typed into the Link window.
    - A battle's end is sent as a direct message because an interaction's
      follow-up lasts 15 minutes and a battle can last longer; a message
      Discord refuses is a WARNING, never a crash.

Call:
    ``asyncio.create_task(Door(core, store, deliver).run(token))``
"""

from __future__ import annotations

import hashlib
import json
import logging
from collections.abc import Awaitable
from collections.abc import Callable
from typing import Any

import aiohttp
import discord
from discord import app_commands

from .core import Core
from .credentials import Token
from .link import BotFile
from .registry import arguments
from .registry import DM
from .registry import Item
from .registry import ITEMS
from .registry import MAX_USER_COMMANDS
from .reply import Button
from .reply import Picker
from .reply import Reply
from .reply import Window

logger = logging.getLogger(__name__)

MENU_LIFETIME_SECONDS = 300
HOST_INSTALL = app_commands.AppInstallationType(guild=True, user=False)
"""Every command is for the host's own server install, not for a person's account."""
Deliver = Callable[[list[dict[str, Any]]], Awaitable[int]]
"""Send pushes to every open page; return how many pages are open."""


def in_dm(interaction: discord.Interaction) -> bool:
    """Return True when the command came in a direct message (no server)."""
    return interaction.guild is None


def names_of(user: discord.abc.User) -> tuple[str, str]:
    """Return the person's (display name, account name); "" for a display name not set."""
    return (user.global_name or "", user.name)


def commands_digest(tree: app_commands.CommandTree) -> str:
    """Return a digest of the command list (slash and right-click) as Discord would be sent it.

    Equal digests mean the same payload. Does not connect to Discord.
    """
    payload = [command.to_dict(tree) for command in tree.get_commands()]
    text = json.dumps(payload, sort_keys=True)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class _Client(discord.Client):
    """The Discord client: the guilds intent only, and a hook that syncs the commands."""

    def __init__(self, door: Door) -> None:
        super().__init__(intents=discord.Intents(guilds=True))
        self.door = door

    async def setup_hook(self) -> None:
        """Sync the commands once the login is done."""
        await self.door.sync_if_changed()

    async def on_ready(self) -> None:
        """Say that the bot is up and under what name."""
        logger.info("bot started as %s", self.user)


class _Tree(app_commands.CommandTree):
    """A command tree that allows the 15 global user commands Discord allows.

    discord.py 2.7.1 stops at 5 (its source and docstring say so) while
    Discord's documentation allows 15, so a global user command is added
    here with the documented limit instead.
    """

    def add_command(self, command: Any, /, **kwargs: Any) -> None:
        """Add a command; a global user command is checked against 15, not 5."""
        if not isinstance(command, app_commands.ContextMenu) or kwargs:
            super().add_command(command, **kwargs)
            return
        key = (command.name, None, command.type.value)
        if key in self._context_menus:
            raise app_commands.CommandAlreadyRegistered(command.name, None)
        total = sum(1 for _, g, t in self._context_menus if g is None and t == key[2])
        if total + 1 > MAX_USER_COMMANDS:
            raise app_commands.CommandLimitReached(
                guild_id=None, limit=MAX_USER_COMMANDS, type=command.type
            )
        self._context_menus[key] = command


class _LinkWindow(discord.ui.Modal):
    """The window the Link item opens: one text field for the code."""

    def __init__(self, door: Door, item: Item) -> None:
        super().__init__(title=item.title)
        self.door = door
        self.item = item
        self.code = discord.ui.TextInput(label="Code", min_length=1, max_length=64)
        self.add_item(self.code)

    async def on_submit(self, interaction: discord.Interaction) -> None:
        """Call the row's method with the typed code; the code is not logged."""
        await self.door.act(self.item, interaction, self.code.value)


class TextWindow(discord.ui.Modal):
    """A window a reply asks for: one text field, starting at the reply's value."""

    def __init__(self, door: Door, window: Window) -> None:
        super().__init__(title=window.title)
        self.door = door
        self.window = window
        self.text = discord.ui.TextInput(
            label=window.label,
            default=window.value,
            min_length=1,
            max_length=window.max_length,
        )
        self.add_item(self.text)

    async def on_submit(self, interaction: discord.Interaction) -> None:
        """Hand the typed text to the door."""
        await self.door.typed(interaction, self.window, self.text.value)


class Door:
    """The bot's connection to Discord and its commands."""

    def __init__(self, core: Core, store: BotFile, deliver: Deliver) -> None:
        """Build the client and the command tree; connect to nothing."""
        self.core = core
        self.store = store
        self.deliver = deliver
        self.client = _Client(self)
        self.tree = _Tree(self.client)
        self._register()
        core.control.subscribe(self.on_push)

    def _register(self) -> None:
        for item in ITEMS:
            self.tree.add_command(self._slash(item))
            self.tree.add_command(self._right_click(item))

    @staticmethod
    def _contexts(item: Item) -> app_commands.AppCommandContext:
        return app_commands.AppCommandContext(
            guild=item.where != DM, dm_channel=True, private_channel=False
        )

    def _slash(self, item: Item) -> app_commands.Command:
        door, param = self, item.param

        async def plain(interaction: discord.Interaction) -> None:
            await door.act(item, interaction, None)

        async def typed(interaction: discord.Interaction, value: str) -> None:
            await door.act(item, interaction, value)

        async def chosen(
            interaction: discord.Interaction, value: str | None = None
        ) -> None:
            await door.act(item, interaction, value)

        if param is None:
            callback = plain
        else:
            callback = chosen if not param.required else typed
            callback = app_commands.rename(value=param.name)(callback)
            callback = app_commands.describe(value=param.description)(callback)
            if param.choices:
                choices = [app_commands.Choice(name=c, value=c) for c in param.choices]
                callback = app_commands.choices(value=choices)(callback)
        return app_commands.Command(
            name=item.slash,
            description=item.summary,
            callback=callback,
            allowed_contexts=self._contexts(item),
            allowed_installs=HOST_INSTALL,
        )

    def _right_click(self, item: Item) -> app_commands.ContextMenu:
        door = self

        async def clicked(
            interaction: discord.Interaction, person: discord.Member | discord.User
        ) -> None:
            logger.debug("%s used on account %d: ignored", item.title, person.id)
            if item.method == "link":
                await interaction.response.send_modal(_LinkWindow(door, item))
                return
            await door.act(item, interaction, None)

        return app_commands.ContextMenu(
            name=item.title,
            callback=clicked,
            type=discord.AppCommandType.user,
            allowed_contexts=self._contexts(item),
            allowed_installs=HOST_INSTALL,
        )

    async def act(
        self, item: Item, interaction: discord.Interaction, value: str | None
    ) -> None:
        """Call the row's ``Core`` method for the asker and answer with its reply.

        The same path serves the slash command and the right-click item.
        Defers first when the row reads files. ``value`` is the one typed
        value (None for a row that takes none).
        """
        if item.defers:
            await interaction.response.defer(ephemeral=True)
        user, dm = interaction.user.id, in_dm(interaction)
        call = arguments(item, user, dm, value, names_of(interaction.user))
        reply = getattr(self.core, item.method)(*call)
        await self.answer(interaction, reply)

    async def answer(self, interaction: discord.Interaction, reply: Reply) -> None:
        """Send the reply's pushes to the open pages, then answer privately.

        Adds the reply's menus and buttons. A reply that edits replaces the
        clicked message; otherwise it is a new private message (a follow-up
        when the interaction was already deferred). Replies in ``then`` follow
        as messages of their own. A reply with a window opens it instead,
        which must be the interaction's first answer. Does not check who
        asked.
        """
        if reply.window is not None:
            await interaction.response.send_modal(TextWindow(self, reply.window))
            return
        pages = await self.deliver(reply.pushes) if reply.pushes else 0
        text = self.core.delivered_text(reply, pages)
        view = self._view(reply)
        done = interaction.response.is_done()
        if reply.edit and done:
            await interaction.edit_original_response(content=text, view=view)
        elif reply.edit:
            await interaction.response.edit_message(content=text, view=view)
        else:
            options: dict[str, Any] = {"ephemeral": True}
            if view is not None:
                options["view"] = view
            if done:
                await interaction.followup.send(text, **options)
            else:
                await interaction.response.send_message(text, **options)
        for more in reply.then:
            options = {"ephemeral": True}
            more_view = self._view(more)
            if more_view is not None:
                options["view"] = more_view
            await interaction.followup.send(more.text, **options)

    def _view(self, reply: Reply) -> discord.ui.View | None:
        if not reply.pickers and not reply.buttons:
            return None
        view = discord.ui.View(timeout=MENU_LIFETIME_SECONDS)
        for picker in reply.pickers:
            select = discord.ui.Select(
                placeholder=picker.placeholder,
                options=[
                    discord.SelectOption(
                        label=o.label,
                        value=o.value,
                        description=o.description or None,
                        default=o.default,
                    )
                    for o in picker.options
                ],
            )
            select.callback = self._pick_callback(select, picker)
            view.add_item(select)
        for spec in reply.buttons:
            button = discord.ui.Button(
                label=spec.label, style=discord.ButtonStyle.primary
            )
            button.callback = self._press_callback(spec)
            view.add_item(button)
        return view

    def _pick_callback(
        self, select: discord.ui.Select, picker: Picker
    ) -> Callable[[discord.Interaction], Awaitable[None]]:
        async def callback(interaction: discord.Interaction) -> None:
            await self.picked(interaction, picker, list(select.values))

        return callback

    def _press_callback(
        self, spec: Button
    ) -> Callable[[discord.Interaction], Awaitable[None]]:
        async def callback(interaction: discord.Interaction) -> None:
            await self.pressed(interaction, spec)

        return callback

    async def picked(
        self, interaction: discord.Interaction, picker: Picker, values: list[str]
    ) -> None:
        """Answer a pick from one of the menus; ``values`` are the chosen values.

        Takes the first value. A mission pick and a kind pick of Show mission
        defer first because they read the game's files; a Single player step
        defers as an update and edits the message. Does nothing for an empty
        ``values``.
        """
        if not values:
            return
        user, dm, value = interaction.user.id, in_dm(interaction), values[0]
        core = self.core
        if picker.kind in ("mission", "missions_kind"):
            await interaction.response.defer(ephemeral=True)
            if picker.kind == "mission":
                reply = core.pick_mission(user, picker.key, value, dm)
            else:
                reply = core.missions(user, value, dm)
        elif picker.kind.startswith("solo_"):
            await interaction.response.defer()
            reply = core.pick_solo(user, picker.kind[5:], picker.key, value, dm)
        elif picker.kind == "option":
            reply = core.pick_option(user, picker.key, value, dm)
        else:
            reply = core.pick_setting(user, picker.key, value, dm)
        await self.answer(interaction, reply)

    async def pressed(self, interaction: discord.Interaction, spec: Button) -> None:
        """Answer a button press by its kind.

        Launch asks the launcher to fly the battle (deferred first, as it
        starts a program); Keep name and Change name answer at once, as
        Change name's window must be the first answer. Another kind is
        logged and left unanswered.
        """
        user, dm = interaction.user.id, in_dm(interaction)
        if spec.kind == "solo_launch":
            await interaction.response.defer()
            reply = self.core.launch_solo(user, spec.key, dm)
        elif spec.kind == "pilot_keep":
            reply = self.core.keep_name(user, dm)
        elif spec.kind == "pilot_change":
            reply = self.core.change_name(user, dm)
        else:
            logger.warning("a button of unknown kind %r was pressed", spec.kind)
            return
        await self.answer(interaction, reply)

    async def typed(
        self, interaction: discord.Interaction, window: Window, text: str
    ) -> None:
        """Answer a window's typed text by the window's kind.

        The pilot name window renames the asker's pilot. Another kind is
        logged and left unanswered.
        """
        user, dm = interaction.user.id, in_dm(interaction)
        if window.kind != "pilot_name":
            logger.warning("a window of unknown kind %r was sent", window.kind)
            return
        await self.answer(interaction, self.core.rename_pilot(user, text, dm))

    async def on_push(self, push: dict[str, Any]) -> None:
        """Send a battle's ending to the linked account by direct message.

        Takes every push the control publishes and acts on ``launch.ended``
        only. A message Discord refuses (the person allows no direct messages
        from the bot, or Discord cannot be reached) is logged at WARNING
        without the token and never raised.
        """
        notice = self.core.end_notice(push)
        if notice is None:
            return
        user_id, line = notice
        try:
            person = await self.client.fetch_user(user_id)
            await person.send(line)
        except Exception as exc:  # whatever Discord answers, the bot keeps running
            logger.warning(
                "could not send the battle's end to account %d by direct message: %s",
                user_id,
                type(exc).__name__,
            )
            return
        logger.info("sent the battle's end to account %d by direct message", user_id)

    async def sync_if_changed(self) -> bool:
        """Send the global command list to Discord when it changed; return whether it did.

        The last list sent is remembered as a digest in ``bot.json``. Raises
        what discord.py raises when Discord refuses the list.
        """
        digest = commands_digest(self.tree)
        if digest == self.store.commands_digest:
            logger.debug("the command list is unchanged; not synced")
            return False
        sent = await self.tree.sync()
        self.store.commands_digest = digest
        self.store.save()
        logger.info("synced %d commands", len(sent))
        return True

    async def run(self, token: Token) -> None:
        """Log in and run until stopped; return when the bot has stopped.

        When Discord refuses the token, logs an ERROR (without the token) and
        returns; when Discord cannot be reached (no network), and for any
        other Discord error, logs an ERROR naming the error's type. Never
        raises for those, so the page that shares the loop keeps serving.
        """
        try:
            async with self.client:
                await self.client.start(token.reveal())
        except discord.LoginFailure:
            logger.error("Discord refused the bot's token; the bot is stopped")
        except discord.DiscordException as exc:
            logger.error("the bot stopped: %s", type(exc).__name__)
        except (aiohttp.ClientError, OSError) as exc:
            logger.error("the bot cannot reach Discord: %s", type(exc).__name__)

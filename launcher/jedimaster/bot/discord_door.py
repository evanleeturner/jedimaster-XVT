"""The one place the bot touches discord.py: the client, the commands, the menus.

Purpose:
    Carry ``core``'s decisions to Discord and back. Each slash command is a
    thin handler that calls ``core`` and sends its reply privately; the
    menus ``core`` asks for become select menus; the client logs in with the
    bot's token and keeps the global command list in step.

Flow:
    ``Door`` builds a client with no intents and a command tree of five
    commands. A handler calls the matching ``Core`` method with the asker's
    account id, sends the reply's pushes to the open pages (``deliver``),
    then answers with ``answer``. A pick from a select menu goes through
    ``Door.picked``. ``Door.run`` logs in and runs until stopped; on start
    ``sync_if_changed`` sends the command list to Discord only when it
    differs from the last one sent.

Invariants:
    - This is the only module that imports discord.py.
    - Every answer is ephemeral (visible to the asker only).
    - No intents are asked for: slash commands need none.
    - ``/missions`` and a mission pick defer first (they read files);
      the others answer at once, inside Discord's three seconds.
    - The token is read from a ``Token`` only to log in and is never logged.

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

from ..page.protocol import MISSION_TYPE_NAMES
from .core import Core
from .core import Picker
from .core import Reply
from .credentials import Token
from .link import BotFile

logger = logging.getLogger(__name__)

MENU_LIFETIME_SECONDS = 300
Deliver = Callable[[list[dict[str, Any]]], Awaitable[int]]
"""Send pushes to every open page; return how many pages are open."""


def in_dm(interaction: discord.Interaction) -> bool:
    """Return True when the command came in a direct message (no server)."""
    return interaction.guild is None


def commands_digest(tree: app_commands.CommandTree) -> str:
    """Return a digest of the command list exactly as Discord would be sent it.

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


class Door:
    """The bot's connection to Discord and its five commands."""

    def __init__(self, core: Core, store: BotFile, deliver: Deliver) -> None:
        """Build the client and the command tree; connect to nothing."""
        self.core = core
        self.store = store
        self.deliver = deliver
        self.client = _Client(self)
        self.tree = app_commands.CommandTree(self.client)
        self._register()

    def _register(self) -> None:
        tree, core = self.tree, self.core
        dm_only = app_commands.allowed_contexts(
            guilds=False, dms=True, private_channels=False
        )
        anywhere = app_commands.allowed_contexts(
            guilds=True, dms=True, private_channels=False
        )
        host_app = app_commands.allowed_installs(guilds=True, users=False)

        @tree.command(name="link", description="Link this launcher to your account")
        @app_commands.describe(code="The code the launcher printed")
        @dm_only
        @host_app
        async def link(interaction: discord.Interaction, code: str) -> None:
            reply = core.link(interaction.user.id, code, in_dm(interaction))
            await self.answer(interaction, reply)

        @tree.command(name="unlink", description="Unlink this launcher")
        @dm_only
        @host_app
        async def unlink(interaction: discord.Interaction) -> None:
            reply = core.unlink(interaction.user.id, in_dm(interaction))
            await self.answer(interaction, reply)

        @tree.command(name="status", description="Show the launcher's status")
        @anywhere
        @host_app
        async def status(interaction: discord.Interaction) -> None:
            reply = core.status(interaction.user.id, in_dm(interaction))
            await self.answer(interaction, reply)

        @tree.command(name="missions", description="List a kind of mission")
        @app_commands.describe(mission_type="The kind of mission")
        @app_commands.rename(mission_type="type")
        @app_commands.choices(
            mission_type=[
                app_commands.Choice(name=name, value=name)
                for name in MISSION_TYPE_NAMES
            ]
        )
        @anywhere
        @host_app
        async def missions(interaction: discord.Interaction, mission_type: str) -> None:
            await interaction.response.defer(ephemeral=True)
            reply = core.missions(interaction.user.id, mission_type, in_dm(interaction))
            await self.answer(interaction, reply)

        @tree.command(name="settings", description="Show and change the settings")
        @dm_only
        @host_app
        async def settings(interaction: discord.Interaction) -> None:
            reply = core.settings(interaction.user.id, in_dm(interaction))
            await self.answer(interaction, reply)

    async def answer(self, interaction: discord.Interaction, reply: Reply) -> None:
        """Send the reply's pushes to the open pages, then answer privately.

        Adds the reply's menus as select menus. Answers with a follow-up when
        the interaction was already deferred. Does not check who asked.
        """
        pages = await self.deliver(reply.pushes) if reply.pushes else 0
        text = self.core.delivered_text(reply, pages)
        options: dict[str, Any] = {"ephemeral": True}
        if reply.pickers:
            options["view"] = self._view(reply.pickers)
        if interaction.response.is_done():
            await interaction.followup.send(text, **options)
        else:
            await interaction.response.send_message(text, **options)

    def _view(self, pickers: tuple[Picker, ...]) -> discord.ui.View:
        view = discord.ui.View(timeout=MENU_LIFETIME_SECONDS)
        for picker in pickers:
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
        return view

    def _pick_callback(
        self, select: discord.ui.Select, picker: Picker
    ) -> Callable[[discord.Interaction], Awaitable[None]]:
        async def callback(interaction: discord.Interaction) -> None:
            await self.picked(interaction, picker, list(select.values))

        return callback

    async def picked(
        self, interaction: discord.Interaction, picker: Picker, values: list[str]
    ) -> None:
        """Answer a pick from one of the menus; ``values`` are the chosen values.

        Takes the first value. A mission pick defers first because it reads
        the game's files. Does nothing for an empty ``values``.
        """
        if not values:
            return
        user, dm = interaction.user.id, in_dm(interaction)
        if picker.kind == "mission":
            await interaction.response.defer(ephemeral=True)
            reply = self.core.pick_mission(user, picker.key, values[0], dm)
        else:
            reply = self.core.pick_setting(user, picker.key, values[0], dm)
        await self.answer(interaction, reply)

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

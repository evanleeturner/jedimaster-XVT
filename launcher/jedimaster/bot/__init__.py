"""The launcher's bot: a Discord front end for the same fixed list as the page.

Purpose:
    Let the player drive their own launcher, and its page, from Discord. The
    bot is a second user of the launcher's fixed list of commands
    (``jedimaster.page``): every Discord command becomes requests on that
    list, so the same checks apply as for the page.

Flow:
    ``link`` keeps the one-time link code and the linked account; ``credentials``
    keeps the bot's token file and hides the token from logs; ``core`` decides
    who may ask, which requests each command sends and what the reply says;
    ``discord_door`` is the only module that touches discord.py; ``cli`` adds
    ``bot setup``, ``bot unlink`` and the ``page --bot`` start.

Invariants:
    - Only ``discord_door`` imports discord.py (the optional ``bot`` extra);
      every other module, and every other command of the launcher, works
      without it.
    - Only the linked account is answered, and every reply is private.
    - The token and the link code are never logged and never in a reply.

Call:
    ``python -m jedimaster bot setup`` then ``python -m jedimaster page --bot``
"""

from __future__ import annotations

import logging

logger = logging.getLogger(__name__)

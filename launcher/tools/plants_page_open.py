"""The planted faults of opening the page: the opener, the kept mission, the bot's asks.

Purpose:
    Break one rule per plant of ``PageOpener`` and its wiring
    (``page/server.py``), the last mission kept for a page that connects
    (``page/control.py``), and which bot replies ask for the page and what
    they say (``bot/core.py``, ``bot/walk.py``, ``bot/discord_door.py``,
    ``bot/cli.py``).

Flow:
    ``plant_faults`` applies each plant alone: write ``new`` over ``old`` in
    ``path``, run the suite, restore, and check that every test in
    ``expect`` failed. An ``expect`` entry matches a test by prefix.

Invariants:
    - Ids are unique across all tables, all starting ``page-open-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_page_open import PAGE_OPEN_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

SRV = "jedimaster/page/server.py"
CTL = "jedimaster/page/control.py"
COR = "jedimaster/bot/core.py"
WLK = "jedimaster/bot/walk.py"
DOR = "jedimaster/bot/discord_door.py"
CLI = "jedimaster/bot/cli.py"
TS = "tests/test_page_server.py::"
TP = "tests/test_bot_page.py::"
TD = "tests/test_bot_door.py::"
TC = "tests/test_bot_core.py::"
TW = "tests/test_bot_walk.py::"
TM = "tests/test_bot_menus.py::"
TL = "tests/test_bot_cli.py::"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return one plant with the table's id prefix."""
    return Plant(f"page-open-{pid}", path, old, new, expect)


WAIT = "        if self._opened_at is not None and now - self._opened_at < OPEN_WAIT_SECONDS:"

OPENER_PLANTS: list[Plant] = [
    plant(
        "opens-every-time",
        SRV,
        WAIT,
        "        if False:",
        TS + "test_the_opener_opens_once_until_the_wait_passes",
        TS + "test_the_start_up_open_counts_as_a_page_on_its_way",
        TP + "test_with_no_page_open_a_pick_opens_one_that_shows_the_pick",
    ),
    plant(
        "wait-at-its-end-still-waits",
        SRV,
        WAIT,
        WAIT.replace("< OPEN", "<= OPEN"),
        TS + "test_the_opener_opens_once_until_the_wait_passes",
    ),
    plant(
        "connected-forgotten",
        SRV,
        '        """Note that a page connected: the next ask may open a page at once."""\n'
        "        self._opened_at = None",
        '        """Note that a page connected: the next ask may open a page at once."""',
        TS + "test_a_page_that_connects_lets_the_next_ask_open_at_once",
        TS + "test_a_page_that_connects_tells_the_applications_opener",
        TP + "test_after_the_opened_page_connects_and_closes_the_next_pick_opens_again",
    ),
    plant(
        "socket-does-not-tell",
        SRV,
        "    request.app[OPENER].connected()\n",
        "",
        TS + "test_a_page_that_connects_tells_the_applications_opener",
        TP + "test_after_the_opened_page_connects_and_closes_the_next_pick_opens_again",
    ),
    plant(
        "unbound-opens",
        SRV,
        "        if self.url is None:",
        "        if False:",
        TS + "test_before_the_port_is_bound_nothing_is_opened",
    ),
    plant(
        "browser-error-escapes",
        SRV,
        "        except webbrowser.Error as exc:",
        "        except KeyError as exc:",
        TS
        + "test_with_no_browser_the_opener_says_no_and_tries_again_next_time[raised]",
    ),
    plant(
        "refusal-taken-as-opened",
        SRV,
        "        if not opened:",
        "        if opened is None:",
        TS
        + "test_with_no_browser_the_opener_says_no_and_tries_again_next_time[refused]",
    ),
    plant(
        "url-logged",
        SRV,
        '        logger.info("opened the page in the browser in %.2f s", self._clock() - began)',
        '        logger.info("opened %s in %.2f s", self.url, self._clock() - began)',
        TS + "test_the_opener_opens_once_until_the_wait_passes",
    ),
    plant(
        "start-up-around-the-opener",
        SRV,
        "            runner.app[OPENER].open()",
        "            webbrowser.open(url)",
        TS + "test_the_start_up_open_counts_as_a_page_on_its_way",
    ),
    plant(
        "url-not-given",
        SRV,
        "        runner.app[OPENER].url = url",
        "        runner.app[OPENER].url = None",
        TS + "test_serve_binds_locally_prints_the_url_and_logs_no_secret",
        TS + "test_the_start_up_open_counts_as_a_page_on_its_way",
    ),
]

KEPT_PLANTS: list[Plant] = [
    plant(
        "shown-not-sent",
        CTL,
        "        pushes = [self._shown] if self._shown is not None else []",
        "        pushes = []",
        TS + "test_a_page_that_connects_after_a_show_gets_the_mission_after_the_status",
        TS + "test_a_late_page_gets_the_shown_mission_then_the_battle_that_flies",
        TP + "test_with_no_page_open_a_pick_opens_one_that_shows_the_pick",
    ),
    plant(
        "battle-before-shown",
        CTL,
        "        return pushes + [running] if running is not None else pushes",
        "        return [running] + pushes if running is not None else pushes",
        TS + "test_a_late_page_gets_the_shown_mission_then_the_battle_that_flies",
    ),
    plant(
        "unlisted-clears-shown",
        CTL,
        'args["id"], kind)\n        return {"shown": False}',
        'args["id"], kind)\n        self._shown = None\n        return {"shown": False}',
        TS + "test_a_page_that_connects_after_a_show_gets_the_mission_after_the_status",
    ),
]

BOT_ASK_PLANTS: list[Plant] = [
    plant(
        "words-always-not-opened",
        COR,
        '            return f"{reply.text} {OPENING if opening else NOT_OPENED}"',
        '            return f"{reply.text} {NOT_OPENED}"',
        TC + "test_when_no_page_is_open_the_reply_says_whether_one_is_opening",
        TD + "test_with_no_page_open_a_pick_opens_the_page_and_says_so",
        TP + "test_with_no_page_open_a_pick_opens_one_that_shows_the_pick",
    ),
    plant(
        "one-page-counted-as-none",
        COR,
        "        if pages > 0:",
        "        if pages > 1:",
        TC + "test_when_no_page_is_open_the_reply_says_whether_one_is_opening",
        TD + "test_with_a_page_open_nothing_is_opened",
    ),
    plant(
        "mission-pick-asks-nothing",
        COR,
        '            f"Showing {title} on the page.", pushes=outcome.pushes, opens_page=True',
        '            f"Showing {title} on the page.", pushes=outcome.pushes, opens_page=False',
        TC + "test_a_mission_or_setting_pick_asks_for_the_page_and_the_rest_do_not",
        TP + "test_with_no_page_open_a_pick_opens_one_that_shows_the_pick",
    ),
    plant(
        "setting-pick-asks-nothing",
        COR,
        "            opens_page=True,\n",
        "            opens_page=False,\n",
        TC + "test_a_mission_or_setting_pick_asks_for_the_page_and_the_rest_do_not",
        TD + "test_with_no_page_open_a_pick_opens_the_page_and_says_so",
    ),
    plant(
        "single-player-asks-nothing",
        WLK,
        "            opens_page=True,\n",
        "            opens_page=False,\n",
        TW + "test_solo_offers_the_six_kinds_in_one_menu",
        TD + "test_single_player_with_no_page_open_opens_it_before_any_push",
    ),
    plant(
        "launch-refusal-asks-nothing",
        WLK,
        '            opens_page=result["message"] == NO_PAGE,',
        "            opens_page=False,",
        TW + "test_a_refused_launch_says_why_and_keeps_the_button",
    ),
    plant(
        "every-launch-asks",
        WLK,
        '            opens_page=result["message"] == NO_PAGE,',
        "            opens_page=True,",
        TW + "test_launch_asks_the_launcher_and_reports_its_words",
        TW + "test_a_launch_refused_for_another_reason_does_not_ask_for_the_page",
    ),
    plant(
        "option-pick-asks-nothing",
        WLK,
        "        return Reply(text, pushes=outcome.pushes, opens_page=True)",
        "        return Reply(text, pushes=outcome.pushes)",
        TW + "test_a_pick_sets_the_option_for_that_player_and_tells_the_pages",
    ),
    plant(
        "door-opens-beside-a-page",
        DOR,
        "        opening = reply.opens_page and pages == 0 and self.open_page()",
        "        opening = reply.opens_page and self.open_page()",
        TD + "test_with_a_page_open_nothing_is_opened",
    ),
    plant(
        "door-counts-only-pushes",
        DOR,
        "        asks = bool(reply.pushes) or reply.opens_page",
        "        asks = bool(reply.pushes)",
        TD + "test_single_player_with_no_page_open_opens_it_before_any_push",
        TM + "test_the_whole_walk_edits_one_message_and_launch_starts_the_battle",
    ),
    plant(
        "door-counts-every-reply",
        DOR,
        "        asks = bool(reply.pushes) or reply.opens_page",
        "        asks = True",
        TD + "test_a_reply_that_does_not_ask_for_the_page_counts_and_opens_nothing",
    ),
    plant(
        "bot-given-no-opener",
        CLI,
        "            Core(control, linker, pilots), store, deliver, lambda: open_page(app)",
        "            Core(control, linker, pilots), store, deliver, lambda: False",
        TL + "test_the_bot_opens_the_page_through_the_pages_application",
    ),
]

PAGE_OPEN_PLANTS: list[Plant] = [*OPENER_PLANTS, *KEPT_PLANTS, *BOT_ASK_PLANTS]

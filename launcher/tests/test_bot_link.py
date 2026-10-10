"""The link code and the linked account.

Purpose:
    Prove the code's shape, its ten minutes, its single use, its void after
    five wrong tries, the way it is compared, what ``bot.json`` keeps, and
    that the code is never logged.

Flow:
    A ``Linker`` over a temporary ``bot.json`` with a clock the test moves.

Invariants:
    - No sleeping: the clock is injected.
    - No Discord, no game data.

Call:
    ``pytest tests/test_bot_link.py``
"""

from __future__ import annotations

import json
import logging
import re
import secrets
from pathlib import Path

import pytest
from botdata import Clock

from jedimaster.bot import link
from jedimaster.bot.link import ALPHABET
from jedimaster.bot.link import BotFile
from jedimaster.bot.link import Linker

logger = logging.getLogger(__name__)


@pytest.fixture
def clock() -> Clock:
    return Clock()


@pytest.fixture
def linker(tmp_path: Path, clock: Clock) -> Linker:
    return Linker(BotFile(tmp_path / "config" / "bot.json"), clock=clock)


def raw(shown: str) -> str:
    return shown.replace("-", "")


def test_the_alphabet_has_thirty_unambiguous_characters():
    assert ALPHABET == "23456789ABCDEFGHJKMNPQRSTVWXYZ"
    assert len(ALPHABET) == len(set(ALPHABET)) == 30


def test_a_code_is_eight_characters_shown_as_two_groups_of_four(linker):
    shown = linker.start()
    assert re.fullmatch(rf"[{ALPHABET}]{{4}}-[{ALPHABET}]{{4}}", shown)


def test_a_code_is_drawn_from_the_secrets_module(tmp_path, clock):
    picked: list[str] = []

    def draw(pool: str) -> str:
        picked.append(pool)
        return pool[len(picked) % len(pool)]

    made = Linker(BotFile(tmp_path / "bot.json"), clock=clock, draw=draw)
    made.start()
    assert picked == [ALPHABET] * 8
    assert Linker(BotFile(tmp_path / "x.json")).draw is secrets.choice


def test_two_codes_differ(linker):
    assert len({linker.start() for _ in range(5)}) == 5


def test_the_right_code_links_and_is_saved(linker, tmp_path):
    code = linker.start()
    assert linker.attempt(7, code) == "linked"
    assert linker.linked_user_id == 7
    saved = json.loads((tmp_path / "config" / "bot.json").read_text(encoding="utf-8"))
    assert saved["linked_user_id"] == 7
    assert BotFile(tmp_path / "config" / "bot.json").linked_user_id == 7


def test_lower_case_padding_and_a_missing_dash_are_accepted(tmp_path, clock):
    variants = {
        "lower": str.lower,
        "no-dash": raw,
        "lower-no-dash": lambda c: raw(c).lower(),
        "padded": lambda c: f"  {c} ",
        "space-for-dash": lambda c: c.replace("-", " "),
    }
    for name, change in variants.items():
        made = Linker(BotFile(tmp_path / f"{name}.json"), clock=clock)
        assert made.attempt(7, change(made.start())) == "linked", name


def test_a_wrong_code_is_refused_and_nothing_is_linked(linker):
    linker.start()
    assert linker.attempt(7, "AAAA-AAAA") == "wrong"
    assert linker.linked_user_id is None


def test_a_code_works_once(linker):
    code = linker.start()
    assert linker.attempt(7, code) == "linked"
    assert linker.attempt(8, code) == "already_linked"
    assert linker.linked_user_id == 7


def test_a_used_code_does_not_link_again_after_an_unlink(linker):
    code = linker.start()
    linker.attempt(7, code)
    linker.unlink()
    assert linker.attempt(8, code) == "no_code"
    assert linker.linked_user_id is None


def test_a_code_is_good_for_just_under_ten_minutes(linker, clock):
    code = linker.start()
    clock.now += 599
    assert linker.attempt(7, code) == "linked"


def test_a_code_expires_at_ten_minutes(linker, clock):
    code = linker.start()
    clock.now += 600
    assert linker.attempt(7, code) == "expired"
    assert linker.linked_user_id is None
    assert linker.attempt(7, code) == "no_code"


def test_the_lifetime_is_ten_minutes():
    assert link.LIFETIME_SECONDS == 600


def test_the_fifth_wrong_try_voids_the_code_and_the_sixth_is_refused(linker, caplog):
    code = linker.start()
    answers = [linker.attempt(5 + n, "BBBB-BBBB") for n in range(5)]
    assert answers == ["wrong"] * 4 + ["void"]
    with caplog.at_level(logging.WARNING):
        assert linker.attempt(7, code) == "void"
    assert linker.linked_user_id is None


def test_four_wrong_tries_leave_the_right_code_good(linker):
    code = linker.start()
    for _ in range(4):
        linker.attempt(5, "BBBB-BBBB")
    assert linker.attempt(7, code) == "linked"


def test_wrong_tries_count_across_accounts(linker):
    code = linker.start()
    for n in range(5):
        linker.attempt(100 + n, "BBBB-BBBB")
    assert linker.attempt(7, code) == "void"


def test_the_void_warning_says_to_restart(linker, caplog):
    linker.start()
    with caplog.at_level(logging.WARNING, logger="jedimaster.bot.link"):
        for _ in range(5):
            linker.attempt(5, "BBBB-BBBB")
    assert "restart the launcher for a new code" in caplog.text


def test_a_new_code_clears_the_void(linker):
    linker.start()
    for _ in range(5):
        linker.attempt(5, "BBBB-BBBB")
    code = linker.start()
    assert linker.attempt(5, "BBBB-BBBB") == "wrong"
    assert linker.attempt(7, code) == "linked"


def test_start_makes_nothing_once_linked(linker):
    linker.attempt(7, linker.start())
    assert linker.start() is None


def test_an_attempt_without_a_code_says_so(linker):
    assert linker.attempt(7, "AAAA-AAAA") == "no_code"


def test_the_code_is_compared_in_constant_time(linker, monkeypatch):
    calls = []
    real = link.hmac.compare_digest

    def counting(a, b):
        calls.append((a, b))
        return real(a, b)

    monkeypatch.setattr(link.hmac, "compare_digest", counting)
    code = linker.start()
    linker.attempt(7, "AAAA-AAAA")
    linker.attempt(7, code)
    assert len(calls) == 2
    assert all(isinstance(a, bytes) and isinstance(b, bytes) for a, b in calls)


def test_the_code_is_never_logged(linker, caplog):
    with caplog.at_level(logging.DEBUG):
        code = linker.start()
        linker.attempt(5, code[::-1])
        linker.attempt(7, code)
        linker.unlink()
    assert code not in caplog.text and raw(code) not in caplog.text
    for line in ("made", "used", "unlinked"):
        assert line in caplog.text


def test_expiry_and_void_are_logged_without_the_code(linker, clock, caplog):
    with caplog.at_level(logging.WARNING):
        code = linker.start()
        clock.now += 601
        linker.attempt(7, code)
    assert "expired" in caplog.text and raw(code) not in caplog.text


def test_unlink_removes_the_link_and_reports_whether_there_was_one(linker, tmp_path):
    assert linker.unlink() is False
    linker.attempt(7, linker.start())
    assert linker.unlink() is True
    assert linker.linked_user_id is None
    assert BotFile(tmp_path / "config" / "bot.json").linked_user_id is None


def test_unlink_with_nothing_linked_writes_no_file(linker, tmp_path):
    linker.unlink()
    assert not (tmp_path / "config" / "bot.json").exists()


def test_bot_json_sits_beside_the_settings_file(tmp_path):
    settings = tmp_path / "somewhere" / "settings.json"
    assert link.bot_file_path(settings) == tmp_path / "somewhere" / "bot.json"


def test_the_digest_is_kept_beside_the_account(tmp_path):
    store = BotFile(tmp_path / "bot.json")
    store.linked_user_id = 7
    store.commands_digest = "abc"
    assert store.save() is True
    again = BotFile(tmp_path / "bot.json")
    assert (again.linked_user_id, again.commands_digest) == (7, "abc")


@pytest.mark.parametrize(
    "text",
    [
        "not json",
        "[1]",
        '{"linked_user_id": "7"}',
        '{"linked_user_id": -3}',
        '{"linked_user_id": true}',
    ],
    ids=["text", "list", "string-id", "negative-id", "bool-id"],
)
def test_a_broken_bot_file_links_nobody(tmp_path, text, caplog):
    path = tmp_path / "bot.json"
    path.write_text(text, encoding="utf-8")
    with caplog.at_level(logging.WARNING):
        store = BotFile(path)
    assert store.linked_user_id is None
    assert path.read_text(encoding="utf-8") == text


def test_a_bot_file_that_cannot_be_written_is_a_warning(tmp_path, caplog):
    blocker = tmp_path / "blocker"
    blocker.write_text("x", encoding="utf-8")
    store = BotFile(blocker / "bot.json")
    store.linked_user_id = 7
    with caplog.at_level(logging.WARNING):
        assert store.save() is False
    assert "cannot write the bot file" in caplog.text
    assert store.linked_user_id == 7

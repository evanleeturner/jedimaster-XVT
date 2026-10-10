"""The bot's token file and the filter that hides the token.

Purpose:
    Prove the token file is made with mode 0600, that an empty token or one
    with spaces is refused without repeating it, that a token never shows
    when printed, and that the log filter hides it in a message, its
    arguments and a traceback, for a record from any logger.

Flow:
    Files in a temporary folder; a handler of our own with the filter.

Invariants:
    - No Discord, no network.
    - The token used here is made up.

Call:
    ``pytest tests/test_bot_credentials.py``
"""

from __future__ import annotations

import io
import logging
import os
import stat
import sys

import pytest

from jedimaster.bot import credentials
from jedimaster.bot.credentials import check_token
from jedimaster.bot.credentials import install_log_filter
from jedimaster.bot.credentials import read_token
from jedimaster.bot.credentials import Token
from jedimaster.bot.credentials import token_path
from jedimaster.bot.credentials import write_token

logger = logging.getLogger(__name__)

SECRET = "made-up.TOKEN_value-12345"
posix_only = pytest.mark.skipif(sys.platform.startswith("win"), reason="no POSIX modes")


def test_the_token_file_sits_beside_the_settings_file(tmp_path):
    settings = tmp_path / "x" / "settings.json"
    assert token_path(settings) == tmp_path / "x" / "bot_token"


@posix_only
def test_the_token_file_is_mode_0600(tmp_path):
    path = tmp_path / "config" / "bot_token"
    write_token(path, SECRET)
    assert stat.S_IMODE(path.stat().st_mode) == 0o600
    assert path.read_text(encoding="utf-8").strip() == SECRET


@posix_only
def test_the_file_is_created_with_the_mode_not_changed_afterwards(
    tmp_path, monkeypatch
):
    seen: list[int] = []
    real_open = os.open

    def watching(path, flags, mode=0o777, **kwargs):
        seen.append(mode)
        return real_open(path, flags, mode, **kwargs)

    monkeypatch.setattr(credentials.os, "open", watching)
    monkeypatch.setattr(
        credentials.os, "chmod", lambda *a, **k: pytest.fail("chmod after creation")
    )
    write_token(tmp_path / "bot_token", SECRET)
    assert seen == [0o600]


@posix_only
def test_an_old_loose_file_is_replaced_by_a_private_one(tmp_path):
    path = tmp_path / "bot_token"
    path.write_text("old\n", encoding="utf-8")
    path.chmod(0o644)
    write_token(path, SECRET)
    assert stat.S_IMODE(path.stat().st_mode) == 0o600
    assert read_token(path).reveal() == SECRET


def test_no_temporary_file_is_left_behind(tmp_path):
    write_token(tmp_path / "bot_token", SECRET)
    assert [p.name for p in tmp_path.iterdir()] == ["bot_token"]


@pytest.mark.parametrize(
    "text", ["", "two words", "tab\there", "line\nbreak", " lead", "trail "]
)
def test_an_empty_token_or_one_with_spaces_is_refused(tmp_path, text):
    assert check_token(text) is not None
    with pytest.raises(ValueError) as caught:
        write_token(tmp_path / "bot_token", text)
    assert text.strip() == "" or text.strip() not in str(caught.value)
    assert not (tmp_path / "bot_token").exists()


def test_a_good_token_passes_the_check():
    assert check_token(SECRET) is None


def test_reading_a_missing_file_gives_none(tmp_path):
    assert read_token(tmp_path / "bot_token") is None


@pytest.mark.parametrize("text", ["", "   \n", "has space\n"])
def test_reading_a_file_with_no_good_token_gives_none_and_a_warning(
    tmp_path, text, caplog
):
    path = tmp_path / "bot_token"
    path.write_text(text, encoding="utf-8")
    with caplog.at_level(logging.WARNING):
        assert read_token(path) is None
    assert "no good" in caplog.text
    assert "space" not in caplog.text.replace("has spaces", "")


def test_reading_drops_the_trailing_newline(tmp_path):
    path = tmp_path / "bot_token"
    path.write_text(SECRET + "\n", encoding="utf-8")
    assert read_token(path).reveal() == SECRET


def test_a_token_never_shows_when_printed():
    token = Token(SECRET)
    for shown in (
        repr(token),
        str(token),
        f"{token}",
        f"{token!r}",
        format(token, ""),
        ascii(token),
    ):
        assert SECRET not in shown and shown == "[token]"
    assert token.reveal() == SECRET


def make_logging(hidden: bool = True):
    stream = io.StringIO()
    handler = logging.StreamHandler(stream)
    handler.setFormatter(logging.Formatter("%(name)s %(levelname)s %(message)s"))
    if hidden:
        install_log_filter(Token(SECRET), [handler])
    root = logging.getLogger("tokentest")
    root.handlers = [handler]
    root.propagate = False
    root.setLevel(logging.DEBUG)
    return root, stream


@pytest.mark.parametrize(
    "name", ["tokentest", "tokentest.discord.http", "tokentest.jedimaster.page"]
)
def test_the_filter_hides_the_token_in_a_message_from_any_logger(name):
    root, stream = make_logging()
    logging.getLogger(name).info("logging in with %s now", SECRET)
    logging.getLogger(name).debug("plain " + SECRET)
    text = stream.getvalue()
    assert SECRET not in text
    assert text.count("[token]") == 2


def test_the_filter_hides_the_token_in_arguments_of_every_kind():
    root, stream = make_logging()
    root.warning("a %s b %r c %s", SECRET, SECRET, {"auth": SECRET})
    assert stream.getvalue() == (
        "tokentest WARNING a [token] b '[token]' c {'auth': '[token]'}\n"
    )


def test_the_filter_hides_the_token_in_a_traceback():
    root, stream = make_logging()
    try:
        raise RuntimeError(f"failed with {SECRET}")
    except RuntimeError:
        root.exception("it failed")
    text = stream.getvalue()
    assert "RuntimeError" in text and SECRET not in text


def test_the_filter_leaves_other_records_alone():
    root, stream = make_logging()
    root.info("nothing secret here: %d", 3)
    assert stream.getvalue() == "tokentest INFO nothing secret here: 3\n"


def test_the_filter_goes_on_the_root_handlers_by_default():
    handler = logging.StreamHandler(io.StringIO())
    logging.getLogger().addHandler(handler)
    try:
        found = install_log_filter(Token(SECRET))
        assert found in handler.filters
    finally:
        logging.getLogger().removeHandler(handler)


def test_the_filter_survives_a_message_that_does_not_format():
    root, stream = make_logging()
    root.info("two %s %s", SECRET)
    assert SECRET not in stream.getvalue()

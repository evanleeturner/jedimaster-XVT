"""Loading a pilot: each case of files, the defaults and their sources, the logs.

Purpose:
    Prove the game's load for each case (both files, the ``.pl2`` alone,
    the ``.plt`` alone, neither, a short file of each kind, a long one):
    its result, its record and the lines it logs; a new pilot's defaults
    and where they come from (the menus' entries and the training lists,
    Balance of Power first or not); the network names and the cuts of a
    text too long for its field; the out-of-range warning; the files
    found in a folder in any letter case.

Flow:
    Build records with ``pilotdata``, load them with made-up defaults or
    a fake install, inspect bytes, events and warnings.

Invariants:
    - No game data: every name, text and mission number is made up.

Call:
    ``pytest tests/test_pilot_load.py``
"""

from __future__ import annotations

import logging

import pytest
from pilotdata import bytes_at
from pilotdata import front
from pilotdata import install
from pilotdata import menu
from pilotdata import record
from pilotdata import value_at

from jedimaster.pilot import BASE_RECORD
from jedimaster.pilot import Defaults
from jedimaster.pilot import FULL_RECORD
from jedimaster.pilot import install_defaults
from jedimaster.pilot import load_files
from jedimaster.pilot import load_pilot
from jedimaster.pilot import struct
from jedimaster.pilot.load import pl2_name
from jedimaster.pilot.load import plt_name

logger = logging.getLogger(__name__)

DEFAULTS = Defaults(
    rating=2, rating_name=b"Rookie", game_name_suffix=b" plays", training=(31, 32, 33)
)
NAME = "Ace0.plt"
GAME_NAMES = ("multiplayer_game_name", "multiplayer_host_name")
TRAINING_SLOTS = (
    ("mission_description_ids", 0),
    ("faction_statistics", 0, "mission_description_ids", 0),
    ("faction_statistics", 1, "mission_description_ids", 0),
    ("faction_statistics", 2, "mission_description_ids", 0),
    ("faction_statistics", 3, "mission_description_ids", 0),
)


def full(**values) -> bytes:
    """Return a full record with ``values`` (top-level names)."""
    return bytes(record(FULL_RECORD, {(k,): v for k, v in values.items()}))


def base(**values) -> bytes:
    """Return a base record with ``values`` (top-level names)."""
    return bytes(record(BASE_RECORD, {(k,): v for k, v in values.items()}))


def texts(load) -> list[str]:
    """Return the load's events as ``WARNING: `` + text or the text."""
    return [
        ("WARNING: " if e.level >= logging.WARNING else "") + e.text
        for e in load.events
    ]


def text(data: bytes, name: str) -> bytes:
    """Return a full record's text field, all its bytes."""
    size = next(m.size for m in struct(FULL_RECORD).members if m.name == name)
    return bytes_at(data, FULL_RECORD, (name,), size)


LOADED = (
    "pilot.record_loaded rating={} missions=0 score=0 faction=0 promo=0 percent=0 "
    "rank_change=0"
)


def test_both_files():
    pl2 = record(
        FULL_RECORD,
        {
            ("rating",): 7,
            ("multiplayer_game_name",): b"Old",
            ("mission_description_ids", 0): 5,
        },
    )
    plt = base(name=b"Ace", rating=9, rating_name=b"Captain")
    load = load_files(NAME, plt, bytes(pl2), DEFAULTS)
    assert (load.result, load.pl2, load.plt) == (1, True, True)
    assert value_at(load.data, FULL_RECORD, ("rating",)) == 9
    assert text(load.data, "rating_name").rstrip(b"\0") == b"Captain"
    assert text(load.data, "multiplayer_game_name").rstrip(b"\0") == b"Old"
    assert text(load.data, "multiplayer_host_name") == bytes(32)
    assert value_at(load.data, FULL_RECORD, ("mission_description_ids", 0)) == 5
    assert texts(load) == [
        'pilot.load_files path="Ace0.plt" expansion=1 base=1',
        LOADED.format(9),
    ]


def test_pl2_alone_loads_with_a_warning(caplog):
    pl2 = full(rating=3, name=b"Solo")
    with caplog.at_level(logging.WARNING):
        load = load_files(NAME, None, pl2, DEFAULTS)
    assert (load.result, load.pl2, load.plt) == (1, True, False)
    assert load.data == pl2
    assert texts(load) == [
        'pilot.load_files path="Ace0.plt" expansion=1 base=0',
        "WARNING: pilot.record_missing",
    ]
    assert "pilot.record_missing" in caplog.text


def test_plt_alone_takes_the_defaults_then_merges():
    plt = base(name=b"Ace", rating=9, rating_name=b"Captain")
    load = load_files(NAME, plt, None, DEFAULTS)
    assert (load.result, load.pl2, load.plt) == (1, False, True)
    assert value_at(load.data, FULL_RECORD, ("rating",)) == 9
    assert text(load.data, "rating_name").rstrip(b"\0") == b"Captain"
    firsts = [value_at(load.data, FULL_RECORD, path) for path in TRAINING_SLOTS]
    assert firsts == [31, 31, 32, 33, 0]
    for name in GAME_NAMES:
        assert text(load.data, name) == b"Ace plays".ljust(32, b"\0")
    assert texts(load) == [
        'pilot.load_files path="Ace0.plt" expansion=0 base=1',
        'pilot.defaults_set training="31,31,32,33"',
        LOADED.format(9),
    ]


def test_neither_file_fails():
    load = load_files(NAME, None, None, DEFAULTS)
    assert (load.result, load.pl2, load.plt) == (0, False, False)
    assert load.data == bytes(struct(FULL_RECORD).size)
    assert texts(load) == ['pilot.load_files path="Ace0.plt" expansion=0 base=0']


def test_short_pl2_fails_with_a_record_of_zeros():
    pl2 = full(rating=3)[:-1]
    load = load_files(NAME, base(rating=9), pl2, DEFAULTS)
    assert (load.result, load.pl2, load.plt) == (0, True, True)
    assert load.data == bytes(struct(FULL_RECORD).size)
    assert texts(load) == ['pilot.load_files path="Ace0.plt" expansion=1 base=1']


def test_short_plt_alone_keeps_the_defaults():
    load = load_files(NAME, base(rating=9)[:-1], None, DEFAULTS)
    assert load.result == 0
    assert value_at(load.data, FULL_RECORD, ("rating",)) == 2
    assert text(load.data, "rating_name").rstrip(b"\0") == b"Rookie"
    assert [value_at(load.data, FULL_RECORD, p) for p in TRAINING_SLOTS] == [
        31,
        31,
        32,
        33,
        0,
    ]
    assert text(load.data, "multiplayer_game_name") == bytes(32)
    assert texts(load)[1:] == ['pilot.defaults_set training="31,31,32,33"']


def test_short_plt_with_pl2_keeps_the_pl2():
    pl2 = full(rating=3)
    load = load_files(NAME, base(rating=9)[:-1], pl2, DEFAULTS)
    assert load.result == 0 and load.data == pl2
    assert len(texts(load)) == 1


def test_longer_files_are_read_for_their_record_size():
    pl2, plt = full(rating=3), base(rating=9, name=b"Ace")
    long = load_files(NAME, plt + b"tail", pl2 + b"tail", DEFAULTS)
    exact = load_files(NAME, plt, pl2, DEFAULTS)
    assert long.result == 1 and long.data == exact.data


def test_record_loaded_names_its_members():
    plt = base(
        rating=5,
        total_missions_played_count=6,
        total_score=7,
        current_faction_id=1,
        current_rating_promo_points=8,
        next_promotion_percent=9,
        promotion_delta=-3,
    )
    load = load_files(NAME, plt, full(), DEFAULTS)
    assert texts(load)[-1] == (
        "pilot.record_loaded rating=5 missions=6 score=7 faction=1 promo=8 "
        "percent=9 rank_change=-3"
    )


@pytest.mark.parametrize(
    ("rating", "faction", "warned"),
    [(25, 0, True), (24, 3, False), (0, 4, True), (-1, -1, False)],
)
def test_out_of_range_is_logged_and_kept(caplog, rating, faction, warned):
    plt = base(rating=rating, current_faction_id=faction)
    with caplog.at_level(logging.WARNING):
        load = load_files(NAME, plt, full(), DEFAULTS)
    line = f"pilot.record_out_of_range rating={rating} faction={faction}"
    assert (texts(load)[-1] == "WARNING: " + line) is warned
    assert (line in caplog.text) is warned
    assert value_at(load.data, FULL_RECORD, ("rating",)) == rating
    assert value_at(load.data, FULL_RECORD, ("current_faction_id",)) == faction


def test_game_names_cut_to_their_field(caplog):
    suffix = Defaults(2, b"R", b"-" * 25, (0, 0, 0))
    with caplog.at_level(logging.WARNING):
        fits = load_files(NAME, base(name=b"A" * 6), None, suffix)
    assert caplog.text == ""
    assert text(fits.data, GAME_NAMES[0]) == b"A" * 6 + b"-" * 25 + b"\0"
    with caplog.at_level(logging.WARNING):
        cut = load_files(NAME, base(name=b"A" * 7), None, suffix)
    assert "run past" in caplog.text
    for name in GAME_NAMES:
        assert text(cut.data, name) == b"A" * 7 + b"-" * 24 + b"\0"


def test_name_without_end_mark_is_taken_whole(caplog):
    size = text(full(), "name").__len__()
    with caplog.at_level(logging.WARNING):
        load = load_files(NAME, base(name=b"N" * size), None, DEFAULTS)
    assert "no byte 0" in caplog.text
    assert text(load.data, GAME_NAMES[1]).rstrip(b"\0") == b"N" * size + b" plays"


def test_texts_stop_at_their_end_mark_and_rating_name_cut(caplog):
    odd = Defaults(2, b"x" * 40, b" vs\0hidden", (0, 0, 0))
    with caplog.at_level(logging.WARNING):
        short = load_files(NAME, base()[:10], None, odd)
    assert text(short.data, "rating_name") == b"x" * 31 + b"\0"
    assert "rating_name" in caplog.text
    load = load_files(NAME, base(name=b"Ace"), None, odd)
    assert text(load.data, GAME_NAMES[0]).rstrip(b"\0") == b"Ace vs"


def test_defaults_from_balance_of_power_first(tmp_path):
    root = install(tmp_path, bop_texts={124: b"BoP rank", 470: b" bop game"})
    defaults = install_defaults(root)
    assert defaults == Defaults(2, b"BoP rank", b" bop game", (21, 22, 23))
    plain = install_defaults(root, balance_of_power=False)
    assert plain == Defaults(2, b"entry 124", b"entry 470", (11, 12, 13))


def test_defaults_fall_back_to_the_install(tmp_path):
    root = install(tmp_path)
    (root / "BalanceOfPower" / "TRAIN" / "imperial.lst").unlink()
    (root / "BalanceOfPower" / "fronttxt.txt").unlink()
    defaults = install_defaults(root)
    assert defaults.training == (21, 12, 23)
    assert defaults.rating_name == b"entry 124"


def test_defaults_edge_cases(tmp_path):
    root = install(tmp_path, bop=None)
    (root / "Train" / "mission.lst").write_bytes(menu())
    (root / "fronttxt.txt").write_bytes(front({})[:40])
    defaults = install_defaults(root)
    assert defaults.training == (11, 12, 0)
    assert defaults.rating_name == defaults.game_name_suffix == b"No text."
    (root / "Train" / "rebel.lst").unlink()
    with pytest.raises(FileNotFoundError, match="rebel.lst"):
        install_defaults(root)
    (root / "fronttxt.txt").unlink()
    with pytest.raises(FileNotFoundError, match="fronttxt.txt"):
        install_defaults(root)


def test_load_pilot_from_a_folder(tmp_path):
    (tmp_path / "ACE0.PLT").write_bytes(base(name=b"Ace", rating=4))
    (tmp_path / "ace0.pl2").write_bytes(full(rating=3))
    load = load_pilot(tmp_path, "Ace0", DEFAULTS)
    assert (load.result, load.pl2, load.plt, load.name) == (1, True, True, "Ace0.plt")
    assert load.record["rating"] == 4
    assert plt_name("x.PLT") == "x.PLT" and pl2_name("x.plt") == "x.pl2"
    missing = load_pilot(tmp_path / "nowhere", "Ace0.plt", DEFAULTS)
    assert (missing.result, missing.pl2, missing.plt) == (0, False, False)

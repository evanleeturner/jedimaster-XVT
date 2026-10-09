"""Finding the movies: the names the game asks for and where each resolves.

Purpose:
    Prove ``movie_names`` gives ``Opening``, the cutscene list's movies in
    its order and ``Flyby1a``, each once (any letter case), and
    ``find_movie`` resolves each the game's way (Balance of Power first,
    then the base game, any letter case), keeps the asked name's case in
    the label, reports a name that does not resolve, and refuses to leave
    the install.

Flow:
    Build installs with ``smkdata.movie_install``; ask; compare.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_movies_find.py``
"""

from __future__ import annotations

import logging

from smkdata import LIST_TEXT
from smkdata import movie_install
from smkdata import smk_header

from jedimaster.movies import find_movie
from jedimaster.movies import movie_names

logger = logging.getLogger(__name__)

SMK = smk_header()


def test_names_are_opening_the_list_in_order_and_flyby1a(tmp_path):
    install = movie_install(tmp_path, {"CUTSCENE.LST": LIST_TEXT.encode()})
    assert movie_names(install) == ["Opening", "intro", "Flyby1a"]


def test_a_name_asked_twice_in_any_case_is_kept_once_at_its_first_place(tmp_path):
    text = "3\nflyby1a\n1 0 1\nt\nd\nOPENING\n1 0 2\nt\nd\nintro\n1 0 3\nt\nd\n"
    install = movie_install(tmp_path, {"cutscene.lst": text.encode()})
    assert movie_names(install) == ["Opening", "flyby1a", "intro"]


def test_without_a_list_the_two_fixed_names_remain(tmp_path):
    install = movie_install(tmp_path, {"opening.smk": SMK})
    assert movie_names(install) == ["Opening", "Flyby1a"]


def test_the_list_is_found_in_the_base_game_too(tmp_path):
    install = movie_install(tmp_path, {"cutscene.lst": LIST_TEXT.encode()}, bop=False)
    assert movie_names(install) == ["Opening", "intro", "Flyby1a"]


def test_the_view_without_balance_of_power_does_not_see_its_list(tmp_path):
    install = movie_install(tmp_path, {"cutscene.lst": LIST_TEXT.encode()})
    assert movie_names(install, balance_of_power=False) == ["Opening", "Flyby1a"]


def test_a_list_entry_cut_short_is_not_asked_for(tmp_path):
    install = movie_install(
        tmp_path, {"cutscene.lst": b"2\nintro\n1 0 7\nthumb\ndesc\nsecond\n"}
    )
    assert movie_names(install) == ["Opening", "intro", "Flyby1a"]


def test_a_file_resolves_in_any_case_and_the_label_keeps_the_asked_case(tmp_path):
    install = movie_install(tmp_path, {"opening.smk": SMK, "OPENING.TXT": b"1\n"})
    found = find_movie(install, "Opening")
    assert found.video is not None and found.video.name == "opening.smk"
    assert found.subtitles is not None and found.subtitles.name == "OPENING.TXT"
    assert found.video_label == "BalanceOfPower/movies/Opening.smk"
    assert found.subtitle_label == "BalanceOfPower/movies/Opening.txt"
    assert find_movie(install, "OPENING").video_label == (
        "BalanceOfPower/movies/OPENING.smk"
    )


def test_balance_of_power_wins_over_the_base_game(tmp_path):
    install = movie_install(tmp_path, {"a.smk": SMK})
    movie_install(tmp_path, {"a.smk": SMK + b"x"}, bop=False)
    found = find_movie(install, "a")
    assert found.video is not None and "BalanceOfPower" in found.video.parts
    assert found.video_label == "BalanceOfPower/movies/a.smk"
    base = find_movie(install, "a", balance_of_power=False)
    assert base.video is not None and "BalanceOfPower" not in base.video.parts
    assert base.video_label == "movies/a.smk"


def test_the_base_game_answers_when_balance_of_power_lacks_the_file(tmp_path):
    install = movie_install(tmp_path, {"b.smk": SMK}, bop=False)
    movie_install(tmp_path, {"other.smk": SMK})
    found = find_movie(install, "b")
    assert found.video_label == "movies/b.smk"


def test_the_video_and_the_subtitles_resolve_on_their_own(tmp_path):
    install = movie_install(tmp_path, {"c.smk": SMK}, bop=False)
    movie_install(tmp_path, {"c.txt": b"1\n"})
    found = find_movie(install, "c")
    assert found.video_label == "movies/c.smk"
    assert found.subtitle_label == "BalanceOfPower/movies/c.txt"


def test_a_name_that_does_not_resolve_is_reported_not_raised(tmp_path):
    install = movie_install(tmp_path, {"c.smk": SMK})
    found = find_movie(install, "nothing")
    assert (found.video, found.video_label) == (None, None)
    assert (found.subtitles, found.subtitle_label) == (None, None)
    assert found.name == "nothing"


def test_a_name_that_leaves_the_install_does_not_resolve(tmp_path, caplog):
    install = movie_install(tmp_path / "XvT", {"c.smk": SMK})
    (tmp_path / "secret.smk").write_bytes(SMK)
    with caplog.at_level(logging.WARNING):
        found = find_movie(install, "../secret")
        also = find_movie(install, "..\\..\\secret")
    assert found.video is None and also.video is None
    assert len([r for r in caplog.records if r.levelno == logging.WARNING]) == 4


def test_a_folder_named_like_a_movie_is_not_a_movie(tmp_path):
    install = movie_install(tmp_path, {"d.smk": SMK})
    (tmp_path / "BalanceOfPower/MOVIES/e.smk").mkdir()
    assert find_movie(install, "e").video is None

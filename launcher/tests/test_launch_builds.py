"""The builds folder: where it is, which build is current, what is kept.

Purpose:
    Prove ``launch.builds`` on made-up build folders: the folder for each
    operating system, the ``current`` file read fresh each time and replaced
    whole, the install order, and the pruning that never removes the current
    build or the one it replaced.

Flow:
    Each test makes build folders holding an ``OpenXvT`` file in a temporary
    folder and calls the functions.

Invariants:
    - No network, no real build.

Call:
    ``pytest tests/test_launch_builds.py``
"""

from __future__ import annotations

import logging
import os
from pathlib import Path

import pytest

from jedimaster.launch import builds as builds_module
from jedimaster.launch.builds import current_engine
from jedimaster.launch.builds import current_name
from jedimaster.launch.builds import default_builds_dir
from jedimaster.launch.builds import installed
from jedimaster.launch.builds import prune
from jedimaster.launch.builds import set_current

logger = logging.getLogger(__name__)


def make_build(builds: Path, name: str, age: int = 0) -> Path:
    """Write a build folder ``name`` that was installed ``age`` seconds ago."""
    folder = builds / name
    folder.mkdir(parents=True)
    (folder / "OpenXvT").write_text("x", encoding="utf-8")
    marker = folder / builds_module.INSTALLED_MARKER
    marker.write_text("x", encoding="utf-8")
    stamp = 1_700_000_000 - age
    os.utime(marker, (stamp, stamp))
    return folder


def test_the_folder_for_each_system(tmp_path, monkeypatch):
    monkeypatch.setattr(Path, "home", lambda: tmp_path / "home")
    home = tmp_path / "home"
    assert default_builds_dir("linux", {}) == home / ".local/share/jedimaster/builds"
    assert (
        default_builds_dir("linux", {"XDG_DATA_HOME": "/x"})
        == Path("/x") / "jedimaster" / "builds"
    )
    assert (
        default_builds_dir("darwin", {})
        == home / "Library/Application Support/jedimaster/builds"
    )
    assert default_builds_dir("win32", {}) == home / "AppData/Local/jedimaster/builds"
    assert (
        default_builds_dir("win32", {"LOCALAPPDATA": "C:/L"})
        == Path("C:/L") / "jedimaster" / "builds"
    )


def test_no_current_file_means_no_current_build(tmp_path):
    assert current_name(tmp_path) is None
    assert current_engine(tmp_path) is None
    assert current_engine(tmp_path / "missing") is None


def test_current_names_the_engine_of_the_build_it_holds(tmp_path):
    make_build(tmp_path, "aaaaaaaaaaaa")
    make_build(tmp_path, "bbbbbbbbbbbb")
    set_current(tmp_path, "aaaaaaaaaaaa")
    assert current_engine(tmp_path) == tmp_path / "aaaaaaaaaaaa" / "OpenXvT"
    set_current(tmp_path, "bbbbbbbbbbbb")
    assert current_engine(tmp_path) == tmp_path / "bbbbbbbbbbbb" / "OpenXvT"


@pytest.mark.parametrize(
    "text", ["", "  \n", "../aaaaaaaaaaaa", "a/b", "nothere", "aaaaaaaaaaaa.partial"]
)
def test_a_current_file_that_names_no_build_reads_as_none(tmp_path, text):
    make_build(tmp_path, "aaaaaaaaaaaa")
    (tmp_path / "aaaaaaaaaaaa.partial").mkdir()
    (tmp_path / "current").write_text(text, encoding="utf-8")
    assert current_name(tmp_path) is None


def test_a_current_file_with_a_path_in_it_is_not_followed_out_of_the_folder(tmp_path):
    builds = tmp_path / "builds"
    make_build(builds, "aaaaaaaaaaaa")
    make_build(builds / "sub", "bbbbbbbbbbbb")
    for text in ("../builds/aaaaaaaaaaaa", "sub/bbbbbbbbbbbb"):
        (builds / "current").write_text(text, encoding="utf-8")
        assert current_name(builds) is None
        assert current_engine(builds) is None


def test_a_partial_install_with_an_engine_is_never_current_or_listed(tmp_path):
    make_build(tmp_path, "aaaaaaaaaaaa.partial")
    (tmp_path / "current").write_text("aaaaaaaaaaaa.partial", encoding="utf-8")
    assert current_name(tmp_path) is None
    assert installed(tmp_path) == []


def test_set_current_goes_through_a_temporary_file_and_a_replace(tmp_path, monkeypatch):
    make_build(tmp_path, "aaaaaaaaaaaa")
    make_build(tmp_path, "bbbbbbbbbbbb")
    seen = []
    real = os.replace

    def spy(source, target):
        seen.append((Path(source).name, Path(target).name))
        real(source, target)

    monkeypatch.setattr(builds_module.os, "replace", spy)
    set_current(tmp_path, "aaaaaaaaaaaa")
    set_current(tmp_path, "bbbbbbbbbbbb")
    assert seen == [
        ("current.tmp", "current"),
        ("previous.tmp", "previous"),
        ("current.tmp", "current"),
    ]


def test_set_current_replaces_the_file_whole_and_remembers_the_old_one(tmp_path):
    make_build(tmp_path, "aaaaaaaaaaaa")
    make_build(tmp_path, "bbbbbbbbbbbb")
    set_current(tmp_path, "aaaaaaaaaaaa")
    set_current(tmp_path, "bbbbbbbbbbbb")
    assert (tmp_path / "current").read_text() == "bbbbbbbbbbbb\n"
    assert (tmp_path / "previous").read_text() == "aaaaaaaaaaaa\n"
    assert sorted(p.name for p in tmp_path.glob("*.tmp")) == []
    set_current(tmp_path, "bbbbbbbbbbbb")
    assert (tmp_path / "previous").read_text() == "aaaaaaaaaaaa\n"


def test_installed_lists_newest_first_and_skips_partials_and_empties(tmp_path):
    make_build(tmp_path, "aaaaaaaaaaaa", age=30)
    make_build(tmp_path, "bbbbbbbbbbbb", age=10)
    make_build(tmp_path, "cccccccccccc", age=20)
    (tmp_path / "dddddddddddd.partial").mkdir()
    (tmp_path / "eeeeeeeeeeee").mkdir()
    (tmp_path / "current").write_text("bbbbbbbbbbbb")
    assert installed(tmp_path) == ["bbbbbbbbbbbb", "cccccccccccc", "aaaaaaaaaaaa"]
    assert installed(tmp_path / "missing") == []


def test_prune_keeps_the_three_newest(tmp_path):
    for age, name in enumerate("abcde"):
        make_build(tmp_path, name * 12, age=age * 10)
    set_current(tmp_path, "a" * 12)
    assert prune(tmp_path) == ["d" * 12, "e" * 12]
    assert installed(tmp_path) == ["a" * 12, "b" * 12, "c" * 12]


def test_prune_never_removes_the_current_build_or_the_one_it_replaced(tmp_path):
    for age, name in enumerate("abcdef"):
        make_build(tmp_path, name * 12, age=age * 10)
    set_current(tmp_path, "d" * 12)
    set_current(tmp_path, "e" * 12)
    assert (tmp_path / "previous").read_text() == "d" * 12 + "\n"
    assert prune(tmp_path) == ["f" * 12]
    assert installed(tmp_path) == [n * 12 for n in "abcde"]

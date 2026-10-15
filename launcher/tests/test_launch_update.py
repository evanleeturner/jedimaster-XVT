"""The update command: what it installs, what it refuses, how it keeps builds.

Purpose:
    Prove ``jedimaster update`` against a stand-in ``gh`` and stand-in CI
    packages: the newest run on main or a named commit is installed and made
    current only after its check, every refusal is one plain sentence with
    exit 1 and leaves the builds as they were, an update to the build already
    current says so, ``--list`` and ``--use`` work, and old builds go.

Flow:
    ``updatedata`` writes the stand-ins; each test runs ``main`` with
    ``--builds`` in a temporary folder and reads the folder, the output and
    the log.

Invariants:
    - No network: ``gh`` is a script first on ``PATH``.

Call:
    ``pytest tests/test_launch_update.py``
"""

from __future__ import annotations

import io
import logging
import os
import tarfile
import types

import pytest
from updatedata import add_run
from updatedata import calls
from updatedata import make_gh
from updatedata import make_package

from jedimaster.__main__ import main
from jedimaster.launch import builds as builds_module
from jedimaster.launch import update

logger = logging.getLogger(__name__)

A = "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678"
B = "b2c3d4e5f60718293a4b5c6d7e8f901234567890"
C = "c3d4e5f60718293a4b5c6d7e8f90123456789012"
D = "d4e5f60718293a4b5c6d7e8f9012345678901234"
E = "e5f60718293a4b5c6d7e8f901234567890123456"


@pytest.fixture
def gh(tmp_path, monkeypatch):
    """A stand-in ``gh`` on ``PATH``, logged in, with no runs yet."""
    return make_gh(tmp_path, monkeypatch)


@pytest.fixture
def builds(tmp_path):
    """The builds folder the command is pointed at (not made yet)."""
    return tmp_path / "builds"


def run_update(builds, *extra) -> int:
    return main(["update", "--builds", str(builds), *extra])


def publish(tmp_path, gh, commit, run_id=None, **changes):
    """List a run of ``commit`` with a package built to ``changes``."""
    folder = tmp_path / "packages"
    folder.mkdir(exist_ok=True)
    package = make_package(folder, commit, **changes)
    add_run(gh, run_id or commit[:5], commit, package)


def stamp_installs(builds):
    """Order the installs by their names' first letters: a is oldest."""
    for index, name in enumerate(sorted(builds_module.installed(builds))):
        marker = builds / name / builds_module.INSTALLED_MARKER
        stamp = 1_700_000_000 + index * 10
        os.utime(marker, (stamp, stamp))


def test_the_newest_main_run_is_installed_and_made_current(
    tmp_path, gh, builds, capsys
):
    publish(tmp_path, gh, A)
    publish(tmp_path, gh, B)
    assert run_update(builds) == 0
    name = B[:12]
    assert capsys.readouterr().out == f"Build {name} is installed and current.\n"
    assert (builds / "current").read_text() == name + "\n"
    assert (builds / name / "OpenXvT").stat().st_mode & 0o100
    assert (builds / name / "ai" / "jedimaster_ai" / "__main__.py").is_file()
    assert sorted(p.name for p in builds.iterdir()) == sorted(["current", name])
    runs = [c[1] for c in calls(gh) if c[:1] == ["api"]]
    assert "branch=main" in runs[0] and "event=push" in runs[0]
    assert "status=success" in runs[0]
    assert builds_module.current_engine(builds) == builds / name / "OpenXvT"


def test_a_named_commit_is_installed_even_when_abbreviated(tmp_path, gh, builds):
    publish(tmp_path, gh, A)
    publish(tmp_path, gh, B)
    assert run_update(builds, "--commit", A[:9]) == 0
    assert (builds / "current").read_text() == A[:12] + "\n"
    queries = [c[1] for c in calls(gh) if c[:1] == ["api"]]
    assert any(f"head_sha={A}" in q for q in queries)


def test_the_build_already_current_is_said_so_without_a_download(
    tmp_path, gh, builds, capsys
):
    publish(tmp_path, gh, A)
    assert run_update(builds) == 0
    capsys.readouterr()
    before = len(calls(gh))
    assert run_update(builds) == 0
    assert capsys.readouterr().out == f"Build {A[:12]} is already current.\n"
    assert [c[:2] for c in calls(gh)[before:]] == [
        ["auth", "status"],
        ["api", calls(gh)[-1][1]],
    ]
    assert not any(c[:2] == ["run", "download"] for c in calls(gh)[before:])


def test_without_gh_one_sentence_and_exit_one(tmp_path, builds, monkeypatch, caplog):
    monkeypatch.setenv("PATH", str(tmp_path / "empty"))
    assert run_update(builds) == 1
    assert update.NO_GH in caplog.text
    assert not builds.exists()


def test_not_logged_in_one_sentence_and_exit_one(tmp_path, monkeypatch, builds, caplog):
    make_gh(tmp_path, monkeypatch, logged_in=False)
    assert run_update(builds) == 1
    assert update.NOT_LOGGED_IN in caplog.text
    assert not builds.exists()


def test_no_successful_run_is_said_plainly(tmp_path, gh, builds, caplog):
    assert run_update(builds) == 1
    assert "No successful Package CI run was found on main." in caplog.text
    publish(tmp_path, gh, A, run_id="9")
    caplog.clear()
    assert run_update(builds, "--commit", B) == 1
    assert f"No successful Package CI run was found for commit {B}." in caplog.text
    caplog.clear()
    assert run_update(builds, "--commit", "ffff") == 1
    assert "gh could not do that: gh: Not Found (HTTP 404)." in caplog.text


def test_a_pull_request_run_is_not_the_newest_on_main(tmp_path, gh, builds):
    publish(tmp_path, gh, A)
    package = make_package(tmp_path, B)
    add_run(gh, "pr", B, package, branch="feature", event="pull_request")
    assert run_update(builds) == 0
    assert (builds / "current").read_text() == A[:12] + "\n"


def test_a_build_that_names_another_commit_is_refused_and_leaves_nothing(
    tmp_path, gh, builds, caplog
):
    publish(tmp_path, gh, A)
    assert run_update(builds) == 0
    publish(tmp_path, gh, B, shown="0.0.0-dev")
    caplog.clear()
    assert run_update(builds) == 1
    assert (
        f"The build {B[:12]} says it is version 0.0.0-dev, a different commit."
        in caplog.text
    )
    assert (builds / "current").read_text() == A[:12] + "\n"
    assert sorted(p.name for p in builds.iterdir()) == sorted(["current", A[:12]])


def test_a_build_whose_ai_program_does_not_match_is_refused(
    tmp_path, gh, builds, caplog
):
    publish(tmp_path, gh, A, engine_wire=12, ai_wire=11)
    assert run_update(builds) == 1
    assert "speaks AI version 12 and its AI program speaks AI version 11" in caplog.text
    assert not (builds / "current").exists()
    assert [p.name for p in builds.iterdir()] == []


def test_a_build_with_no_ai_program_is_refused(tmp_path, gh, builds, caplog):
    publish(tmp_path, gh, A, ai_wire=None)
    assert run_update(builds) == 1
    assert "No AI program was found beside this game build" in caplog.text
    assert [p.name for p in builds.iterdir()] == []


def test_a_run_without_the_package_is_said_plainly(tmp_path, gh, builds, caplog):
    add_run(gh, "1", A, None)
    assert run_update(builds) == 1
    assert "gh could not do that" in caplog.text
    assert [p.name for p in builds.iterdir()] == []


def test_macos_says_updating_is_not_built_yet(
    tmp_path, gh, builds, caplog, monkeypatch
):
    monkeypatch.setattr(update, "sys", types.SimpleNamespace(platform="darwin"))
    publish(tmp_path, gh, A)
    assert run_update(builds) == 1
    assert "Updating on macOS is not built yet." in caplog.text
    assert [c for c in calls(gh) if c[:1] == ["api"]] == []


def test_the_artifact_and_package_kind_per_system():
    assert update.artifact_for("linux") == ("openxvt-linux-x86_64-release", ".tar.xz")
    assert update.artifact_for("win32") == ("openxvt-windows-x86_64-release", ".zip")
    with pytest.raises(update.UpdateError, match="macOS"):
        update.artifact_for("darwin")


def test_a_package_with_a_path_outside_its_folder_is_refused(
    tmp_path, gh, builds, caplog
):
    evil = tmp_path / "evil.tar.xz"
    with tarfile.open(evil, "w:xz") as archive:
        info = tarfile.TarInfo("../escape.txt")
        info.size = 1
        archive.addfile(info, io.BytesIO(b"x"))
    add_run(gh, "1", A, evil)
    assert run_update(builds) == 1
    assert "outside its folder" in caplog.text
    assert not (tmp_path / "escape.txt").exists()
    assert [p.name for p in builds.iterdir()] == []


def test_list_marks_the_current_build(tmp_path, gh, builds, capsys):
    assert run_update(builds, "--list") == 0
    assert "No builds are installed." in capsys.readouterr().out
    publish(tmp_path, gh, A)
    publish(tmp_path, gh, B)
    assert run_update(builds, "--commit", A) == 0
    assert run_update(builds, "--commit", B) == 0
    stamp_installs(builds)
    capsys.readouterr()
    assert run_update(builds, "--list") == 0
    assert capsys.readouterr().out == f"* {B[:12]}\n  {A[:12]}\n"


def test_use_goes_back_to_an_installed_build_after_checking_it(
    tmp_path, gh, builds, capsys
):
    publish(tmp_path, gh, A)
    publish(tmp_path, gh, B)
    assert run_update(builds, "--commit", A) == 0
    assert run_update(builds, "--commit", B) == 0
    capsys.readouterr()
    assert run_update(builds, "--use", A[:6]) == 0
    assert capsys.readouterr().out == f"Now using build {A[:12]}.\n"
    assert (builds / "current").read_text() == A[:12] + "\n"
    assert (builds / "previous").read_text() == B[:12] + "\n"


def test_use_refuses_an_unknown_build_and_one_that_fails_its_check(
    tmp_path, gh, builds, caplog
):
    publish(tmp_path, gh, A)
    publish(tmp_path, gh, B)
    assert run_update(builds, "--commit", A) == 0
    assert run_update(builds, "--commit", B) == 0
    assert run_update(builds, "--use", "ffff") == 1
    assert "No installed build starts with ffff." in caplog.text
    assert run_update(builds, "--use", "zz") == 1
    assert update.NOT_A_COMMIT in caplog.text
    (builds / A[:12] / "ai" / "jedimaster_ai" / "__main__.py").write_text(
        "print('ai_wire=3')\n", encoding="utf-8"
    )
    caplog.clear()
    assert run_update(builds, "--use", A[:8]) == 1
    assert "speaks AI version 11 and its AI program speaks AI version 3" in caplog.text
    assert (builds / "current").read_text() == B[:12] + "\n"


def test_only_the_newest_three_installs_stay_but_current_and_previous_do(
    tmp_path, gh, builds
):
    for commit in (A, B, C):
        publish(tmp_path, gh, commit)
        assert run_update(builds, "--commit", commit) == 0
        stamp_installs(builds)
    assert run_update(builds, "--use", A[:12]) == 0
    publish(tmp_path, gh, D)
    assert run_update(builds, "--commit", D) == 0
    stamp_installs(builds)
    assert (builds / "previous").read_text() == A[:12] + "\n"
    assert sorted(builds_module.installed(builds)) == [A[:12], B[:12], C[:12], D[:12]]
    publish(tmp_path, gh, E)
    assert run_update(builds, "--commit", E) == 0
    stamp_installs(builds)
    assert sorted(builds_module.installed(builds)) == [C[:12], D[:12], E[:12]]

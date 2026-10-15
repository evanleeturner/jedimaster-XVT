"""The staged folder the game package carries: the program and its license, no junk."""

from __future__ import annotations

import importlib.util
import pathlib
import subprocess
import sys

TOOL = pathlib.Path(__file__).resolve().parents[1] / "tools" / "stage_package.py"
spec = importlib.util.spec_from_file_location("stage_package", TOOL)
stage_package = importlib.util.module_from_spec(spec)
sys.modules["stage_package"] = stage_package
spec.loader.exec_module(stage_package)

REPO = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO / "ai" / "jedimaster_ai"


def files_under(folder: pathlib.Path) -> set[str]:
    return {p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file()}


def test_the_program_is_copied_whole_without_compiled_files(tmp_path) -> None:
    (SOURCE / "__pycache__").mkdir(exist_ok=True)
    (SOURCE / "__pycache__" / "x.cpython-311.pyc").write_bytes(b"x")
    stage_package.stage(tmp_path)
    expected = {
        p.relative_to(SOURCE).as_posix()
        for p in SOURCE.rglob("*")
        if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc"
    }
    staged = tmp_path / "jedimaster_ai"
    assert files_under(staged) == expected
    assert "__main__.py" in expected and "events.json" in expected
    assert "behaviors/attack.py" in expected
    for name in expected:
        assert (staged / name).read_bytes() == (SOURCE / name).read_bytes()


def test_the_license_is_the_mit_text_with_the_copyright_line(tmp_path) -> None:
    stage_package.stage(tmp_path)
    text = (tmp_path / "licenses" / "jedimaster_ai.txt").read_text(encoding="utf-8")
    assert text.startswith("MIT License\n\nCopyright (c) 2026 Evan Lee Turner\n")
    assert "<year>" not in text and "Permission is hereby granted" in text


def test_a_second_run_replaces_the_first_and_leaves_other_files(tmp_path) -> None:
    stage_package.stage(tmp_path)
    (tmp_path / "jedimaster_ai" / "old.py").write_text("x")
    (tmp_path / "other.txt").write_text("keep")
    stage_package.stage(tmp_path)
    assert not (tmp_path / "jedimaster_ai" / "old.py").exists()
    assert (tmp_path / "other.txt").read_text() == "keep"


def test_a_folder_without_the_program_is_refused(tmp_path) -> None:
    empty = tmp_path / "empty"
    empty.mkdir()
    try:
        stage_package.stage(tmp_path / "out", program=empty)
    except FileNotFoundError as exc:
        assert "no AI program" in str(exc)
    else:
        raise AssertionError("an empty folder was staged")


def test_the_command_line_stages_and_exits_0(tmp_path) -> None:
    done = subprocess.run(
        [sys.executable, str(TOOL), str(tmp_path / "out")],
        capture_output=True,
        text=True,
        check=False,
    )
    assert done.returncode == 0
    assert (tmp_path / "out" / "jedimaster_ai" / "__main__.py").is_file()

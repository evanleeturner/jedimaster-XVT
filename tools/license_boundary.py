#!/usr/bin/env python3
"""Refuse a file outside engine/ that reaches into the engine.

Purpose: engine/ is the GPLv3 game engine; everything outside it is this
project's own, under MIT or CC BY 4.0, and stays so only while it is a
separate program that starts the engine or talks to it across a process
boundary (README.md, REUSE.toml). A file outside engine/ that includes an
engine header, links an engine library, or carries the decompiled game's
tag lines has crossed that boundary, and this check refuses it.

Flow:
  1. list the repository's files with git: tracked, plus untracked files
     that are not ignored, so a new file is caught before its first commit
  2. keep the files outside engine/, minus this check itself
  3. read each as UTF-8 text (a file that is not is binary, and skipped) and
     match every line against the rules that apply to its name
  4. print each finding as path:line: rule: text, then a summary line

Invariants:
  - reads only; never changes a file or the index
  - a file inside engine/ is never judged: that side of the wall is GPL
  - stdlib only

Does not follow a CMake call across lines: a library named on the same line
as target_link_libraries, or alone on its own line, is caught; one named
three lines below the call is not.

Call: license_boundary.py [--repo PATH] [--log-level LEVEL]
  -> exit 0 clean, 1 findings, 2 git failed
"""

from __future__ import annotations

import argparse
import logging
import re
import subprocess
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

REPO_DEFAULT = Path(__file__).resolve().parent.parent
ENGINE_PREFIX = "engine/"

# Files outside engine/ that spell the patterns below on purpose.
EXEMPT_PATHS = frozenset({"tools/license_boundary.py"})

C_FAMILY = (".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".m", ".mm")
BUILD_FILES = ("CMakeLists.txt", ".cmake", "meson.build", "Makefile", ".mk")

# The engine's CMake library targets (engine/CMakeLists.txt, engine/aeron/).
ENGINE_LIBRARIES = (
    r"(?:xvt_core|xvt_remaster|lucas_opt|opt2gltf_lib|aeron(?:_[a-z0-9_]+)?)"
)

# Each rule: its code, the regex one line must match, and the file names or
# suffixes it applies to (None applies to every text file).
RULES: tuple[tuple[str, re.Pattern[str], tuple[str, ...] | None], ...] = (
    (
        "include",
        re.compile(r"^\s*#\s*include\s*[<\"](?:engine/|xvt[/_.]|aeron[/.])"),
        C_FAMILY,
    ),
    (
        "link",
        re.compile(
            r"target_link_libraries\s*\(.*\b" + ENGINE_LIBRARIES + r"\b"
            r"|^\s*" + ENGINE_LIBRARIES + r"\s*\)?\s*$"
            r"|add_subdirectory\s*\(\s*[\"']?engine\b"
            r"|find_package\s*\(\s*(?:xvt|aeron)\b"
        ),
        BUILD_FILES,
    ),
    (
        "tag",
        re.compile(r"//\s*(?:FUNCTION|GLOBAL):\s*XVT\b"),
        None,
    ),
)


def repository_files(repo: Path) -> list[str]:
    """Return the repository's file paths relative to repo, as git lists them.

    Tracked files and untracked files that no ignore rule covers, so a file
    not yet committed is judged too. Raises CalledProcessError when git fails.
    """
    output = subprocess.run(
        [
            "git",
            "-C",
            str(repo),
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return [path for path in output.split("\0") if path]


def judged_files(repo: Path) -> list[str]:
    """Return the paths this check judges: outside engine/, present, not exempt."""
    kept = []
    for path in repository_files(repo):
        if path.startswith(ENGINE_PREFIX) or path in EXEMPT_PATHS:
            continue
        if not (repo / path).is_file():
            logger.debug("skipping %s: listed by git but not a file here", path)
            continue
        kept.append(path)
    return kept


def rule_applies(names: tuple[str, ...] | None, path: str) -> bool:
    """Return True when a rule's file names or suffixes cover path (None covers all)."""
    if names is None:
        return True
    name = Path(path).name
    return name in names or Path(path).suffix in names


def findings_in(repo: Path, path: str) -> list[str]:
    """Return the finding lines for one file; empty when clean or binary."""
    try:
        text = (repo / path).read_bytes().decode("utf-8")
    except UnicodeDecodeError:
        logger.debug("skipping %s: not UTF-8 text", path)
        return []
    rules = [
        (code, pattern) for code, pattern, names in RULES if rule_applies(names, path)
    ]
    found = []
    for number, line in enumerate(text.splitlines(), start=1):
        for code, pattern in rules:
            if pattern.search(line):
                found.append(f"{path}:{number}: {code}: {line.strip()}")
                logger.debug("finding %s at %s:%s", code, path, number)
    return found


def main(argv: list[str]) -> int:
    """Parse arguments, judge every file outside engine/, print the verdict."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", type=Path, default=REPO_DEFAULT)
    parser.add_argument("--log-level", default="INFO")
    options = parser.parse_args(argv)
    logging.basicConfig(level=options.log_level, format="%(levelname)s %(message)s")

    repo = options.repo.resolve()
    try:
        files = judged_files(repo)
    except subprocess.CalledProcessError as error:
        logger.error("git could not list %s: %s", repo, error.stderr.strip())
        return 2
    logger.info("judging %s files outside %s in %s", len(files), ENGINE_PREFIX, repo)

    findings = [line for path in files for line in findings_in(repo, path)]
    for line in findings:
        print(line)
    if findings:
        logger.error(
            "%s findings: a file outside engine/ reaches into the engine", len(findings)
        )
        return 1
    logger.info("clean: nothing outside engine/ reaches into the engine")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

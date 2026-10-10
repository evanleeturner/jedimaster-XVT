"""Plant faults in the page's TypeScript, HTML and CSS; prove its tests catch each.

Purpose:
    A test counts only after it has failed on a planted fault. Each plant in
    ``plants_page_web`` breaks one piece of the page that a ``node --test``
    test or a browser test guards; the runner applies it, runs the tests,
    records which failed, and restores the file.

Flow:
    1. Snapshot every planted file; build; run both suites: they must pass,
       and their test names are collected.
    2. For each plant: check its ``old`` text occurs exactly once, write
       ``new`` in its place, run ``npm test``; build and run
       ``npm run test:browser`` too when the plant names a browser test,
       restore the file, and check that each test the plant names failed.
    3. Report each plant, and any test no plant made fail; build again, run
       both suites: they must pass, and every file must match its snapshot
       byte for byte.

    A test is named ``node: <title>`` or ``browser: <title>``; a plant's
    ``expect`` entries match by prefix.

Invariants:
    - Files are always restored, also on error or Ctrl-C (``finally``).
    - Exit status 0 only when every plant failed its named tests, every test
      failed under some plant, the suites are green after the restore and
      the tree is restored.

Call:
    ``python tools/plant_page_web.py [--only ID ...]``
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from plants_common import Plant
from plants_page_web import WEB_PLANTS

logger = logging.getLogger("plant_page_web")

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "page"
# The browser tests start the launcher from the source tree with this Python.
ENV = dict(os.environ, JEDIMASTER_PYTHON=sys.executable, PYTHONDONTWRITEBYTECODE="1")


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _npm(*args: str, extra_env: dict[str, str] | None = None) -> str:
    """Run ``npm <args>`` in the page folder; return its output.

    Raises ``RuntimeError`` when ``run build`` fails, so a plant that does not
    compile is reported instead of being tested against a stale build.
    """
    proc = subprocess.run(
        ["npm", *args],
        cwd=PAGE,
        capture_output=True,
        text=True,
        env={**ENV, **(extra_env or {})},
        check=False,
    )
    if args[:2] == ("run", "build") and proc.returncode != 0:
        raise RuntimeError(
            f"the build failed: {proc.stdout[-600:]}{proc.stderr[-600:]}"
        )
    return proc.stdout


def node_results() -> dict[str, bool]:
    """Run ``npm test``; return ``{"node: <title>": passed}``."""
    out = _npm("test", "--", "--test-reporter=tap")
    results: dict[str, bool] = {}
    for line in out.splitlines():
        for prefix, passed in (("ok ", True), ("not ok ", False)):
            if line.startswith(prefix) and " - " in line:
                title = line.split(" - ", 1)[1].split(" # ")[0].strip()
                results[f"node: {title}"] = passed
                break
    return results


def _walk(suite: dict, found: dict[str, bool]) -> None:
    for spec in suite.get("specs", []):
        found[f"browser: {spec['title']}"] = bool(spec["ok"])
    for child in suite.get("suites", []):
        _walk(child, found)


def browser_results() -> dict[str, bool]:
    """Build, run ``npm run test:browser``; return ``{"browser: <title>": passed}``."""
    _npm("run", "build")
    with tempfile.TemporaryDirectory() as folder:
        report = Path(folder) / "report.json"
        _npm(
            "run",
            "test:browser",
            "--",
            "--reporter=json",
            extra_env={"PLAYWRIGHT_JSON_OUTPUT_NAME": str(report)},
        )
        results: dict[str, bool] = {}
        if report.is_file():
            for suite in json.loads(report.read_text(encoding="utf-8"))["suites"]:
                _walk(suite, results)
        return results


def wants_browser(plant: Plant) -> bool:
    """Return True when the plant names a browser test or plants the page itself."""
    page_files = ("page/src/main.ts", "page/public/")
    return any(e.startswith("browser:") for e in plant.expect) or plant.path.startswith(
        page_files
    )


def apply(plant: Plant) -> set[str]:
    """Plant one fault, run the tests, restore; return the failed test names."""
    path = ROOT / plant.path
    original = path.read_text(encoding="utf-8")
    count = original.count(plant.old)
    if count != 1:
        raise RuntimeError(f"{plant.id}: old text found {count} times in {plant.path}")
    try:
        path.write_text(original.replace(plant.old, plant.new), encoding="utf-8")
        results = node_results()
        if wants_browser(plant):
            results |= browser_results()
    finally:
        path.write_text(original, encoding="utf-8")
    return {name for name, passed in results.items() if not passed}


def main(argv: list[str] | None = None) -> int:
    """Run every plant; return 0 when each was caught and the tree restored."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", nargs="*", help="plant ids to run")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    plants = [p for p in WEB_PLANTS if not args.only or p.id in args.only]
    files = sorted({ROOT / p.path for p in WEB_PLANTS})
    before = {f: _sha(f) for f in files}
    baseline = node_results() | browser_results()
    if not baseline or not all(baseline.values()):
        print(f"baseline is not green: {[n for n, ok in baseline.items() if not ok]}")
        return 1
    covered: set[str] = set()
    caught_all = True
    try:
        for plant in plants:
            failed = apply(plant)
            covered |= failed
            missing = [
                e for e in plant.expect if not any(f.startswith(e) for f in failed)
            ]
            status = "CAUGHT" if not missing and failed else "MISSED"
            caught_all &= status == "CAUGHT"
            logger.info("%s %s: %d tests failed", status, plant.id, len(failed))
            for name in missing:
                logger.info("    expected failure did not happen: %s", name)
    finally:
        after = node_results() | browser_results()
    uncovered = [t for t in baseline if t not in covered] if not args.only else []
    restored = all(_sha(f) == before[f] for f in files)
    green = bool(after) and all(after.values())
    print(f"plants: {len(plants)}, tests: {len(baseline)}")
    print(f"tests failed under some plant: {len(set(baseline) & covered)}")
    for test in uncovered:
        print(f"  never failed: {test}")
    print(f"suites green after restore: {green}; files restored: {restored}")
    ok = caught_all and not uncovered and green and restored
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

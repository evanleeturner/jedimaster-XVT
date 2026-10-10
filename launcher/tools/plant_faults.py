"""Plant faults in the code, one at a time, and prove the tests catch each.

Purpose:
    A test counts only after it has failed on a planted fault. Each plant
    below breaks one piece of code a test guards; the runner applies it, runs
    the whole suite, records which tests failed, and restores the file.

Flow:
    1. Snapshot every planted file; run the suite once: it must pass.
    2. For each plant: check its ``old`` text occurs exactly once, write
       ``new`` in its place (regenerating the schema files when the plant
       touches a schema generator), run the suite, restore the file (and
       the generated schema files), and check that each test the plant
       names failed.
    3. Report each plant, the tests it failed, and any collected test that no
       plant made fail; run the suite again: it must pass, and every file
       must match its snapshot byte for byte.

    The plants live in ``plants_mission``, ``plants_setup``,
    ``plants_lists_read``, ``plants_lists_output``, ``plants_icons``,
    ``plants_text_lines``, ``plants_text_read``, ``plants_text_output``,
    ``plants_fonts``, ``plants_pictures``, ``plants_pilot``,
    ``plants_models_read``, ``plants_models_draw``,
    ``plants_models_output``, ``plants_movies_read``,
    ``plants_movies_subtitles``, ``plants_movies_output``, ``plants_page`` and
    ``plants_bot``;
    ``PLANTS`` joins them in that order.

Invariants:
    - Files are always restored, also on error or Ctrl-C (``finally``).
    - Exit status 0 only when every plant failed its named tests, every
      collected test failed under some plant, and the tree is restored.

Call:
    ``python tools/plant_faults.py [--only ID ...] [--markdown FILE]``
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import os
import shutil
import subprocess
import sys
from pathlib import Path

from plants_bot import BOT_PLANTS
from plants_briefing import BRIEFING_PLANTS
from plants_common import Plant
from plants_fonts import FONTS_PLANTS
from plants_icons import ICONS_PLANTS
from plants_lists_output import LISTS_OUTPUT_PLANTS
from plants_lists_read import LISTS_READ_PLANTS
from plants_mission import MISSION_PLANTS
from plants_models_draw import MODELS_DRAW_PLANTS
from plants_models_output import MODELS_OUTPUT_PLANTS
from plants_models_read import MODELS_READ_PLANTS
from plants_movies_output import MOVIES_OUTPUT_PLANTS
from plants_movies_read import MOVIES_READ_PLANTS
from plants_movies_subtitles import SUBTITLE_PLANTS
from plants_page import PAGE_PLANTS
from plants_pictures import PICTURES_PLANTS
from plants_pilot import PILOT_PLANTS
from plants_setup import SETUP_PLANTS
from plants_text_lines import TEXT_LINES_PLANTS
from plants_text_output import TEXT_OUTPUT_PLANTS
from plants_text_read import TEXT_READ_PLANTS

ROOT = Path(__file__).resolve().parents[1]
PYTHON = sys.executable
# A planted file and its restored original often have the same size and the
# same mtime second, which would let Python load a stale .pyc of the other
# version; so no bytecode is written or kept while plants run.
ENV = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")


def clear_bytecode() -> None:
    """Delete every __pycache__ folder under the project."""
    for cache in ROOT.rglob("__pycache__"):
        shutil.rmtree(cache, ignore_errors=True)


logger = logging.getLogger("plant_faults")

SCHEMA_FILES = (
    ROOT / "schema/mission.schema.json",
    ROOT / "schema/lists.schema.json",
    ROOT / "schema/icons.schema.json",
    ROOT / "schema/text.schema.json",
    ROOT / "schema/fonts.schema.json",
    ROOT / "schema/pictures.schema.json",
    ROOT / "schema/pilot.schema.json",
    ROOT / "schema/models.schema.json",
    ROOT / "schema/movies.schema.json",
    ROOT / "schema/control.schema.json",
)

PLANTS: list[Plant] = [
    *MISSION_PLANTS,
    *SETUP_PLANTS,
    *LISTS_READ_PLANTS,
    *LISTS_OUTPUT_PLANTS,
    *ICONS_PLANTS,
    *TEXT_LINES_PLANTS,
    *TEXT_READ_PLANTS,
    *TEXT_OUTPUT_PLANTS,
    *FONTS_PLANTS,
    *PICTURES_PLANTS,
    *PILOT_PLANTS,
    *MODELS_READ_PLANTS,
    *MODELS_DRAW_PLANTS,
    *MODELS_OUTPUT_PLANTS,
    *MOVIES_READ_PLANTS,
    *SUBTITLE_PLANTS,
    *MOVIES_OUTPUT_PLANTS,
    *PAGE_PLANTS,
    *BOT_PLANTS,
    *BRIEFING_PLANTS,
]


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_suite() -> tuple[int, set[str]]:
    """Run pytest on the whole suite; return (exit code, failed test ids)."""
    proc = subprocess.run(
        [PYTHON, "-m", "pytest", "-q", "-p", "no:cacheprovider", "-rfE"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        env=ENV,
    )
    failed = set()
    for line in proc.stdout.splitlines():
        if line.startswith(("FAILED tests/", "ERROR tests/")):
            node = line.split(" ", 1)[1].split(" - ")[0].strip()
            failed.add(node)
    return proc.returncode, failed


def collect() -> list[str]:
    """Return every collected test id."""
    proc = subprocess.run(
        [PYTHON, "-m", "pytest", "--collect-only", "-q", "-p", "no:cacheprovider"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
        env=ENV,
    )
    return [line for line in proc.stdout.splitlines() if "::" in line]


def regenerate_schema() -> None:
    """Rewrite the generated schema files from the (possibly planted) models."""
    subprocess.run(
        [PYTHON, "tools/gen_schema.py"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        env=ENV,
    )


def apply(plant: Plant) -> set[str]:
    """Plant one fault, run the suite, restore; return the failed test ids."""
    path = ROOT / plant.path
    original = path.read_text(encoding="utf-8")
    count = original.count(plant.old)
    if count != 1:
        raise RuntimeError(f"{plant.id}: old text found {count} times in {plant.path}")
    schemas = {s: s.read_bytes() for s in SCHEMA_FILES}
    try:
        path.write_text(original.replace(plant.old, plant.new), encoding="utf-8")
        if plant.regen_schema:
            regenerate_schema()
        _, failed = run_suite()
    finally:
        path.write_text(original, encoding="utf-8")
        for schema, before in schemas.items():
            schema.write_bytes(before)
    return failed


def main(argv: list[str] | None = None) -> int:
    """Run every plant; return 0 when each was caught and the tree restored."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", nargs="*", help="plant ids to run")
    parser.add_argument("--markdown", help="also write a markdown table here")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    plants = [p for p in PLANTS if not args.only or p.id in args.only]
    files = sorted({ROOT / p.path for p in PLANTS} | set(SCHEMA_FILES))
    before = {f: _sha(f) for f in files}
    clear_bytecode()
    code, failed = run_suite()
    if code != 0:
        print(f"baseline suite is not green: {sorted(failed)}")
        return 1
    tests = collect()
    caught_all = True
    covered: set[str] = set()
    rows = []
    for plant in plants:
        failed = apply(plant)
        covered |= failed
        missing = [e for e in plant.expect if not any(f.startswith(e) for f in failed)]
        status = "CAUGHT" if not missing and failed else "MISSED"
        caught_all &= status == "CAUGHT"
        rows.append((plant, status, sorted(failed), missing))
        logger.info("%s %s: %d tests failed", status, plant.id, len(failed))
        for name in missing:
            logger.info("    expected failure did not happen: %s", name)
    code, failed = run_suite()
    restored = all(_sha(f) == before[f] for f in files)
    uncovered = [t for t in tests if t not in covered] if not args.only else []
    print(f"plants: {len(plants)}, caught: {sum(r[1] == 'CAUGHT' for r in rows)}")
    caught_tests = len(set(tests) & covered)
    print(f"tests collected: {len(tests)}, failed under some plant: {caught_tests}")
    for test in uncovered:
        print(f"  never failed: {test}")
    print(f"suite green after restore: {code == 0}; files restored: {restored}")
    if args.markdown:
        lines = [
            "| plant | file | tests that failed |",
            "| --- | --- | --- |",
        ]
        for plant, status, names, _ in rows:
            short = ", ".join(n.split("::", 1)[-1] for n in names)
            lines.append(f"| {plant.id} ({status}) | {plant.path} | {short} |")
        Path(args.markdown).write_text("\n".join(lines) + "\n", encoding="utf-8")
    ok = caught_all and not uncovered and code == 0 and restored
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

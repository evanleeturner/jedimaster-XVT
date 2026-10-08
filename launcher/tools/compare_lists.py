"""Derive the game's view of every listed list and diff it against its sheet.

Purpose:
    The acceptance fence of the list reader: for each sheet named in
    ``sheets.md``, read the list the game reads, derive the game's view for
    the sheet's view, render it, and diff it against the sheet. Image sheets
    also check their log's ``image.registered`` lines (file and ``rle=``
    flag, in file order), ``image.open_failed`` lines and the read's end;
    the sound sheet checks its log's ``sound.effect_load_failed`` pairs.

Flow:
    1. Parse the table of ``sheets.md``: sheet, kind, view (``base`` reads
       the install as if Balance of Power were hidden), install file.
    2. From the sheet's name derive the game path the game reads: a menu by
       mission type and view; a sequence through that menu's entry; the
       fixed lists by name. Resolve it, and check it is the listed file.
    3. Read, derive the view, render, diff; check the log where asked.
    4. Print one line per difference and a summary.

Invariants:
    - The answer folder and the install are required options; nothing about
      the game's files is written into this tool.
    - An image's ``compressed`` outcome depends on its pixels, which this
      package does not read; by default it is taken from the log's
      ``image.registered`` lines (``compressed=0`` with ``rle=1``: the
      game's coding did not shrink the bitmap). ``--no-log-compression``
      leaves every bitmap ``compresses`` True and so shows that dependency.
    - Exit status 0 only when every sheet matches and every log check
      passes; a sheet that cannot be read counts as a difference.

Call:
    ``python tools/compare_lists.py --answers DIR --install DIR
    [--sheets FILE] [--context N] [--no-log-compression]``
"""

from __future__ import annotations

import argparse
import difflib
import logging
import re
import sys
from dataclasses import dataclass
from dataclasses import replace
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from jedimaster.lists import files  # noqa: E402
from jedimaster.lists import game  # noqa: E402
from jedimaster.lists import reader  # noqa: E402
from jedimaster.lists import render  # noqa: E402
from jedimaster.lists.text import ListFormatError  # noqa: E402

logger = logging.getLogger("compare_lists")

TYPES_BY_NUMBER = {t.number: t.name for t in game.MISSION_TYPES.values()}
FIELD = re.compile(r'(\w+)=(?:"([^"]*)"|(\S+))')


@dataclass(frozen=True)
class Sheet:
    """One row of sheets.md."""

    path: str
    kind: str
    view: str
    install_file: str


def parse_sheets(text: str) -> list[Sheet]:
    """Return the table rows of sheets.md whose first cell names a sheet."""
    rows = []
    for line in text.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) == 4 and cells[0].endswith(".txt"):
            rows.append(Sheet(*cells))
    return rows


def log_fields(log: str, event: str) -> list[dict[str, str]]:
    """Return the fields of each log line holding ``event``, in order."""
    found = []
    for line in log.splitlines():
        if f" {event} " in line + " ":
            fields = FIELD.findall(line)
            found.append({key: quoted or bare for key, quoted, bare in fields})
    return found


class Checker:
    """Runs one sheet; collects difference messages."""

    def __init__(self, install: Path, answers: Path, log_compression: bool) -> None:
        self.install = install
        self.answers = answers
        self.log_compression = log_compression
        self.problems: list[str] = []
        self.from_log = 0

    def resolve(self, sheet: Sheet, game_path: str) -> tuple[Path, str]:
        bop = sheet.view == "bop"
        found = files.resolve(self.install, game_path, balance_of_power=bop)
        if found is None:
            raise ListFormatError(f"{game_path} does not resolve")
        rel = found.relative_to(self.install).as_posix()
        if rel != sheet.install_file:
            self.problems.append(
                f"resolved {rel}, sheets.md names {sheet.install_file}"
            )
        return found, files.sheet_file(self.install, game_path, found)

    def menu(self, sheet: Sheet, number: str, view: str) -> tuple[game.GameMenu, str]:
        mission_type = TYPES_BY_NUMBER[int(number)]
        game_path = game.menu_game_path(mission_type, view)
        bop = sheet.view == "bop"
        found = files.resolve(self.install, game_path, balance_of_power=bop)
        if found is None:
            raise ListFormatError(f"{game_path} does not resolve")
        return game.menu_view(reader.read_menu(found), mission_type, view), game_path

    def render(self, sheet: Sheet, log: str) -> str:
        stem = Path(sheet.path).stem
        parts = stem.split("-")
        if sheet.kind == "menu":
            game_path = game.menu_game_path(TYPES_BY_NUMBER[int(parts[1])], parts[2])
            found, label = self.resolve(sheet, game_path)
            view, _ = self.menu(sheet, parts[1], parts[2])
            return render.render_menu(view, game_path, label)
        if sheet.kind == "sequence":
            menu, menu_path = self.menu(sheet, parts[1], parts[2])
            index = int(parts[3])
            entry = menu.entries[index]
            game_path = game.sequence_game_path(menu.mission_type, entry.file)
            found, label = self.resolve(sheet, game_path)
            view = game.sequence_view(reader.read_sequence(found))
            return render.render_sequence(
                view, game_path, label, (menu_path, index, entry.id)
            )
        game_path = {
            "ships": "frontres\\frntspec.lst",
            "sounds": "sfx\\sfx.lst",
            "cutscenes": "movies\\cutscene.lst",
            "awards": "frontres\\campawds.lst",
        }.get(sheet.kind, f"frontres\\{'-'.join(parts[1:])}.lst")
        found, label = self.resolve(sheet, game_path)
        if sheet.kind == "images":
            return self.images(sheet, found, game_path, label, log)
        if sheet.kind == "ships":
            return render.render_ships(
                game.ships_view(reader.read_ships(found)), game_path, label
            )
        if sheet.kind == "sounds":
            return self.sounds(found, game_path, label, log)
        if sheet.kind == "cutscenes":
            view = game.cutscenes_view(reader.read_cutscenes(found))
            return render.render_cutscenes(view, game_path, label)
        if sheet.kind == "awards":
            view = game.awards_view(reader.read_awards(found))
            return render.render_awards(view, game_path, label)
        raise ListFormatError(f"unknown kind {sheet.kind}")

    def images(
        self, sheet: Sheet, found: Path, game_path: str, label: str, log: str
    ) -> str:
        images = reader.read_images(found)
        bitmaps = files.install_bitmaps(self.install, images, sheet.view == "bop")
        registered = log_fields(log, "image.registered")
        if self.log_compression:
            gave_up = {
                r["file"]
                for r in registered
                if r["rle"] != "0" and r["compressed"] == "0"
            }
            for word in gave_up & set(bitmaps):
                bitmaps[word] = replace(bitmaps[word], compresses=False)
        view = game.images_view(images, bitmaps)
        if self.log_compression:
            self.from_log = sum(1 for i in view.images if i.bitmap in gave_up)
        mine = [
            (images.groups[i].bitmap.text, str(images.groups[i].flag_value))
            for i in view.registered
        ]
        theirs = [(r["file"], r["rle"]) for r in registered]
        if mine != theirs:
            self.problems.append(
                f"log image.registered differs: {_first_difference(mine, theirs)}"
            )
        missing = [images.groups[i].bitmap.text for i in view.missing]
        failed = [r["file"] for r in log_fields(log, "image.open_failed")]
        if missing != failed:
            self.problems.append(f"log image.open_failed {failed} != missing {missing}")
        ends = [r["end"] for r in log_fields(log, "image.list_done")]
        if ends != [images.end]:
            self.problems.append(f"log end {ends} != reader end {images.end}")
        return render.render_images(view, game_path, label)

    def sounds(self, found: Path, game_path: str, label: str, log: str) -> str:
        sounds = reader.read_sounds(found)
        view = game.sounds_view(sounds, loadable=())
        mine = [
            (sounds.pairs[i].name.text, sounds.pairs[i].wav.text) for i in view.attempts
        ]
        theirs = [
            (r["effect"], r["file"])
            for r in log_fields(log, "sound.effect_load_failed")
        ]
        if mine != theirs:
            self.problems.append(
                f"log sound.effect_load_failed differs: {_first_difference(mine, theirs)}"
            )
        return render.render_sounds(view, game_path, label)


def _first_difference(mine: list, theirs: list) -> str:
    for i, (a, b) in enumerate(zip(mine, theirs, strict=False)):
        if a != b:
            return f"at {i}: reader {a} log {b}"
    return f"reader has {len(mine)}, log has {len(theirs)}"


def compare(
    sheets: list[Sheet],
    answers: Path,
    install: Path,
    context: int,
    log_compression: bool,
) -> int:
    """Check each sheet; print each difference; return the count that match.

    Prints, for each sheet that differs, its log problems and its diff head,
    then a line counting the images whose ``compressed`` outcome came from
    the logs. Does not stop at the first difference.
    """
    same = 0
    from_log = 0
    for sheet in sheets:
        rel = Path(*Path(sheet.path).parts[1:])
        text_path = answers / rel
        log_path = text_path.with_suffix(".log")
        checker = Checker(install, answers, log_compression)
        try:
            expected = text_path.read_text(encoding="latin-1")
            log = log_path.read_text(encoding="latin-1") if log_path.exists() else ""
            text = checker.render(sheet, log)
        except (ListFormatError, OSError, IndexError, KeyError, ValueError) as exc:
            print(f"ERROR {sheet.path}: {exc}")
            continue
        diff = list(
            difflib.unified_diff(
                expected.splitlines(),
                text.splitlines(),
                str(rel),
                "rendered",
                n=0,
                lineterm="",
            )
        )
        from_log += checker.from_log
        if not diff and not checker.problems:
            same += 1
            logger.info("same: %s", sheet.path)
            continue
        print(
            f"DIFF {sheet.path} ({len(diff)} diff lines, {len(checker.problems)} log problems)"
        )
        for problem in checker.problems:
            print(f"  {problem}")
        if diff:
            print("\n".join(diff[:context]))
    print(f"{from_log} images took their compressed outcome from the logs")
    return same


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when every sheet matches."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--answers", required=True, help="the answer sheets' folder")
    parser.add_argument("--install", required=True, help="the install folder")
    parser.add_argument(
        "--sheets", help="the sheet table (default: ../sheets.md beside --answers)"
    )
    parser.add_argument("--context", type=int, default=20)
    parser.add_argument("--no-log-compression", action="store_true")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING)
    answers = Path(args.answers)
    table = Path(args.sheets) if args.sheets else answers.parent / "sheets.md"
    sheets = parse_sheets(table.read_text(encoding="utf-8"))
    same = compare(
        sheets, answers, Path(args.install), args.context, not args.no_log_compression
    )
    print(f"{same} of {len(sheets)} sheets match")
    return 0 if sheets and same == len(sheets) else 1


if __name__ == "__main__":
    sys.exit(main())

"""The install's model files: the list of game names, each resolved and read.

Purpose:
    List every model file name of the model folders (the base game's
    ``ivfiles`` and Balance of Power's ``IVFILES``), resolve each name the
    way the game does for one view, and read each file with ``read_opt``.

Flow:
    ``model_names`` lists both folders (any letter case), keeps the
    ``.opt`` files, lowercases them, writes each as ``ivfiles\\<name>``,
    joins the two without repeats and sorts them like the pictures' list
    (``pictures.game.name_order``). ``models_view`` resolves and reads
    each name into a ``ModelEntry``; ``load_model`` reads one file.

Invariants:
    - Every view runs the whole list: a name that does not resolve in the
      view is ``missing``; one whose file the reader refuses is
      ``not_loaded`` (with a WARNING); nothing is guessed.
    - ``file`` is the sheets' label: the game name lowercased with forward
      slashes, behind ``BalanceOfPower/`` when it resolved there.

Call:
    ``view = models_view(install); view.models[0].model.version``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from pathlib import Path

from ..install import BALANCE_OF_POWER
from ..install import child_in_any_case
from ..lists.files import resolve
from ..lists.files import sheet_file
from ..pictures.game import name_order
from .body import ModelFormatError
from .model import OptModel
from .reader import read_opt
from .regroup import regroup
from .regroup import REGROUPED_VERSIONS

logger = logging.getLogger(__name__)

FOLDER = "ivfiles"
"""The model folder, in the install and in ``BalanceOfPower/``."""
SUFFIX = b".opt"
LOADED = "loaded"
MISSING = "missing"
NOT_LOADED = "not_loaded"


@dataclass
class ModelEntry:
    """One game name of the list in one view: where it resolved, what was read."""

    name: str
    status: str
    path: Path | None
    file: str | None
    model: OptModel | None
    error: Exception | None


@dataclass
class ModelsView:
    """Every model name of an install, resolved and read for one view."""

    install: Path
    balance_of_power: bool
    models: list[ModelEntry]


def _opt_names(folder: Path | None) -> list[str]:
    """Return the lowercased ``.opt`` file names of a folder (none if absent)."""
    if folder is None or not folder.is_dir():
        return []
    names = []
    for entry in sorted(os.listdir(folder)):
        raw = os.fsencode(entry).lower()
        if raw.endswith(SUFFIX) and (folder / entry).is_file():
            names.append(os.fsdecode(raw))
    logger.debug("%s: %d models", folder, len(names))
    return names


def model_names(install: str | os.PathLike[str]) -> list[str]:
    """Return the models' list: ``ivfiles\\<name>`` for both folders' files.

    Every file name ending ``.opt`` (any letter case) in the install's
    ``ivfiles`` and ``BalanceOfPower/IVFILES`` (each folder found in any
    letter case), lowercased (ASCII letters only), joined without repeats,
    sorted by ``name_order``. Returns ``[]`` when neither folder exists.
    Does not read the files.
    """
    install = Path(install)
    base = child_in_any_case(install, FOLDER)
    bop = child_in_any_case(install, BALANCE_OF_POWER)
    bop_folder = child_in_any_case(bop, FOLDER) if bop is not None else None
    names = set(_opt_names(base)) | set(_opt_names(bop_folder))
    listed = sorted((f"{FOLDER}\\{name}" for name in names), key=name_order)
    logger.info("models: %d names", len(listed))
    return listed


def load_model(name: str, path: Path, label: str) -> ModelEntry:
    """Return the model ``name`` read from ``path``, labelled ``label``.

    ``not_loaded`` (with the error, and a WARNING) when ``read_opt``
    refuses the file, the load-time rewrite of a version 0 or 1 model
    refuses it, or it cannot be read, else ``loaded`` with the model.
    Does not draw the model.
    """
    try:
        model = read_opt(path)
        if model.version in REGROUPED_VERSIONS:
            regroup(model)
    except (ModelFormatError, OSError) as exc:
        logger.warning("model %s: not loaded: %s", name, exc)
        return ModelEntry(name, NOT_LOADED, path, label, None, exc)
    logger.debug("model %s: version %d from %s", name, model.version, path)
    return ModelEntry(name, LOADED, path, label, model, None)


def read_model(install: Path, name: str, balance_of_power: bool) -> ModelEntry:
    """Return one name of the list resolved and read for a view.

    ``missing`` when the name does not resolve to a file in the view, else
    what ``load_model`` returns, labelled as the sheets label it. Does not
    check the name against the list.
    """
    path = resolve(install, name, balance_of_power)
    if path is None or not path.is_file():
        logger.debug("model %s: missing", name)
        return ModelEntry(name, MISSING, None, None, None, None)
    return load_model(name, path, sheet_file(install, name, path))


def models_view(
    install: str | os.PathLike[str], balance_of_power: bool = True
) -> ModelsView:
    """Return every name of ``model_names`` resolved and read for a view.

    Names resolve in ``BalanceOfPower/`` first unless ``balance_of_power``
    is False, any letter case. Always returns one ``ModelEntry`` per name,
    in the list's order. Does not check that the install is one.
    """
    install = Path(install)
    models = [
        read_model(install, name, balance_of_power) for name in model_names(install)
    ]
    counts = {
        s: sum(m.status == s for m in models) for s in (LOADED, MISSING, NOT_LOADED)
    }
    logger.info("models view, balance of power %s: %s", balance_of_power, counts)
    return ModelsView(install, balance_of_power, models)

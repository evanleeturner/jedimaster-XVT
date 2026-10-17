"""Fence for the weaving flag: every answer built from a weave's move says so.

The engine moves a weaving craft's shield power to its engines only while
its answers carry ``Actions.weaving``. A behavior that flies a weave and
leaves the flag off would weave at full shields, and no behavior's own test
would notice a new one. So this scan reads every module of the program: a
function that keeps the move ``weave.start`` or ``weave.fly`` returns may
use it only

- in a call that sets ``weaving=True`` itself,
- in a call to a builder: a function, in any module, whose body sets
  ``weaving=True``,
- in a call to a function that returns a ``weave.Move`` (a move turned
  clear of a big ship),
- in a log call, or in a comparison (``move is not None``).

Any other use of the move fails the scan, and so does a weave call whose
result is not unpacked into three names, or a weave function imported under
another name, since the scan could not follow the move.
"""

from __future__ import annotations

import ast
from pathlib import Path

import jedimaster_ai

PACKAGE = Path(jedimaster_ai.__file__).parent
WEAVE_CALLS = ("start", "fly")
# The functions that fly a weave today; the scan must find each, so a
# renamed module or a changed call shape cannot leave it reading nothing.
WEAVERS = {
    "behaviors/attack.py:fight",
    "behaviors/attack.py:_seek",
    "behaviors/attack.py:_evade_ordered",
    "behaviors/board.py:_approach",
    "behaviors/home.py:_evade",
    "behaviors/patrol.py:evade",
    "behaviors/kamikaze.py:_flown",
    "behaviors/kamikaze.py:_evade_ordered",
}
# Ends a weave and makes no move: found, and kept nothing.
ENDS_ONLY = {"behaviors/home.py:_clear"}


def _sets_weaving(node: ast.AST) -> bool:
    return any(
        isinstance(sub, ast.keyword)
        and sub.arg == "weaving"
        and isinstance(sub.value, ast.Constant)
        and sub.value.value is True
        for sub in ast.walk(node)
    )


def _is_weave_call(node: ast.AST) -> bool:
    return (
        isinstance(node, ast.Call)
        and isinstance(node.func, ast.Attribute)
        and isinstance(node.func.value, ast.Name)
        and node.func.value.id == "weave"
        and node.func.attr in WEAVE_CALLS
    )


def _root(node: ast.AST) -> ast.AST:
    while isinstance(node, ast.Attribute):
        node = node.value
    return node


class _Program:
    """The parsed modules, their builders and their move functions."""

    def __init__(self, modules: dict[str, ast.Module]) -> None:
        self.modules = modules
        self.builders: dict[str, set[str]] = {}
        self.movers: dict[str, set[str]] = {}
        for name, tree in modules.items():
            stem = Path(name).stem
            for function in tree.body:
                if not isinstance(function, ast.FunctionDef):
                    continue
                if _sets_weaving(function):
                    self.builders.setdefault(stem, set()).add(function.name)
                returns = function.returns
                if returns is not None and ast.unparse(returns) in (
                    "weave.Move",
                    "Move",
                ):
                    self.movers.setdefault(stem, set()).add(function.name)

    def _allowed(self, stem: str, call: ast.Call) -> bool:
        if any(
            keyword.arg == "weaving"
            and isinstance(keyword.value, ast.Constant)
            and keyword.value.value is True
            for keyword in call.keywords
        ):
            return True
        func = call.func
        if isinstance(func, ast.Name):
            module, name = stem, func.id
        elif isinstance(func, ast.Attribute) and isinstance(func.value, ast.Name):
            module, name = func.value.id, func.attr
            if module == "logger":
                return True
        else:
            return False
        return name in self.builders.get(module, set()) | self.movers.get(module, set())

    def faults(self) -> tuple[set[str], set[str], list[str]]:
        """Return the functions that keep a weave's move, those that call a
        weave and keep none, and every fault found, each naming its
        function and line."""
        keeping: set[str] = set()
        ending: set[str] = set()
        found: list[str] = []
        for name, tree in self.modules.items():
            found += _renamed_imports(name, tree)
            stem = Path(name).stem
            for function in ast.walk(tree):
                if not isinstance(function, ast.FunctionDef):
                    continue
                where = f"{name}:{function.name}"
                calls = [node for node in ast.walk(function) if _is_weave_call(node)]
                if not calls:
                    continue
                moves = _kept_moves(function, where, found)
                (keeping if moves else ending).add(where)
                found += self._misused(stem, function, where, moves)
        return keeping, ending, found

    def _misused(
        self, stem: str, function: ast.FunctionDef, where: str, moves: set[str]
    ) -> list[str]:
        cleared: set[int] = set()
        found = []
        for node in ast.walk(function):
            if isinstance(node, ast.Call):
                used = [
                    _root(value)
                    for value in [*node.args, *(k.value for k in node.keywords)]
                    if isinstance(_root(value), ast.Name) and _root(value).id in moves
                ]
                if used and not self._allowed(stem, node):
                    found.append(
                        f"{where} line {node.lineno}: the weave's move reaches "
                        f"{ast.unparse(node.func)}, which sets no weaving=True"
                    )
                cleared.update(id(name) for name in used)
            elif isinstance(node, ast.Compare):
                cleared.update(
                    id(side)
                    for side in [node.left, *node.comparators]
                    if isinstance(side, ast.Name)
                )
        for node in ast.walk(function):
            if (
                isinstance(node, ast.Name)
                and node.id in moves
                and isinstance(node.ctx, ast.Load)
                and id(node) not in cleared
            ):
                found.append(
                    f"{where} line {node.lineno}: {node.id}, the weave's move, is "
                    "used outside an answer the scan can check"
                )
        return found


def _kept_moves(function: ast.FunctionDef, where: str, found: list[str]) -> set[str]:
    """Return the names a function binds to a weave call's move, and add a
    fault for each weave call not unpacked into three names."""
    moves = set()
    unpacked = set()
    for node in ast.walk(function):
        if not isinstance(node, ast.Assign) or not _is_weave_call(node.value):
            continue
        for target in node.targets:
            if isinstance(target, ast.Tuple) and len(target.elts) == 3:
                unpacked.add(id(node.value))
                move = target.elts[1]
                if isinstance(move, ast.Name) and move.id != "_":
                    moves.add(move.id)
    for node in ast.walk(function):
        if _is_weave_call(node) and id(node) not in unpacked:
            found.append(
                f"{where} line {node.lineno}: a weave call not unpacked into "
                "three names"
            )
    return moves


def _renamed_imports(name: str, tree: ast.Module) -> list[str]:
    """Return a fault for each weave function or weave module brought in
    under a name the scan does not follow."""
    found = []
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom):
            if (node.module or "").split(".")[-1] == "weave":
                found.append(f"{name} line {node.lineno}: imports from weave by name")
            for alias in node.names:
                if alias.name == "weave" and alias.asname not in (None, "weave"):
                    found.append(
                        f"{name} line {node.lineno}: weave imported as {alias.asname}"
                    )
    return found


def _program() -> _Program:
    return _Program(
        {
            path.relative_to(PACKAGE).as_posix(): ast.parse(path.read_text())
            for path in sorted(PACKAGE.rglob("*.py"))
            if path.name != "weave.py"
        }
    )


def test_every_answer_built_from_a_weave_move_says_weaving() -> None:
    keeping, ending, found = _program().faults()
    assert WEAVERS <= keeping
    assert ENDS_ONLY <= ending
    assert found == []


FORGETS = """
def builder(move):
    return Actions(steer=move.steer, weaving=True)

def clear(move) -> weave.Move:
    return move

def good(view, flown, state, tick):
    flown, move, state = weave.fly(view, flown, True, state, tick)
    if move is not None:
        move = clear(move)
        logger.debug("x", move.steer.yaw)
        return builder(move)
    return Actions(steer=move.steer, countermeasure=move.flare, weaving=True)

def forgets(view, flown, state, tick):
    flown, move, state = weave.start(view, 1, flown, state, tick)
    return Actions(steer=move.steer, throttle=1)

def leaks(view, flown, state, tick):
    flown, move, state = weave.fly(view, flown, True, state, tick)
    steer = move.steer
    return Actions(steer=steer, weaving=True)

def keeps_whole(view, flown, state, tick):
    flown = weave.fly(view, flown, True, state, tick)
    return flown

def ends(view, flown, state, tick):
    flown, _, state = weave.fly(view, flown, False, state, tick)
    return flown
"""


def test_the_scan_catches_a_weave_move_sent_without_the_flag() -> None:
    keeping, ending, found = _Program({"made_up.py": ast.parse(FORGETS)}).faults()
    assert keeping == {"made_up.py:good", "made_up.py:forgets", "made_up.py:leaks"}
    assert ending == {"made_up.py:keeps_whole", "made_up.py:ends"}
    assert found == [
        "made_up.py:forgets line 18: the weave's move reaches Actions, which sets "
        "no weaving=True",
        "made_up.py:leaks line 22: move, the weave's move, is used outside an "
        "answer the scan can check",
        "made_up.py:keeps_whole line 26: a weave call not unpacked into three names",
    ]


def test_the_scan_catches_a_weave_function_imported_by_name() -> None:
    source = "from ..weave import fly\nfrom .. import weave as w\n"
    _, _, found = _Program({"made_up.py": ast.parse(source)}).faults()
    assert found == [
        "made_up.py line 1: imports from weave by name",
        "made_up.py line 2: weave imported as w",
    ]

"""Think: the answer to one question, decided craft by craft.

Purpose:
    The program's one decision. For each craft in the question, in order,
    the behavior its order code picks decides, and the decisions become the
    answer.

Invariants:
    - The answer depends only on the question and the behavior table: the
      same question gets the same answer on every machine, and again after
      a rewind. No clock, no environment, no memory between questions.
    - DEBUG records each craft's view as it came and its answer as it goes,
      the notebook in hex; lines are written only when DEBUG is on.
"""

from __future__ import annotations

import logging
import typing as t

from .behaviors import Behavior
from .orders import behavior_for
from .orders import BEHAVIORS
from .world import Answer
from .world import CraftAnswer
from .world import CraftView
from .world import Question

logger = logging.getLogger(__name__)


def think(
    question: Question, behaviors: t.Mapping[int, Behavior] = BEHAVIORS
) -> Answer:
    """Return the answer to a question; ``behaviors`` maps order codes."""
    debug = logger.isEnabledFor(logging.DEBUG)
    if debug:
        logger.debug(
            "think.question tick=%d craft=%d", question.tick, len(question.craft)
        )
    answers = []
    for view in question.craft:
        if debug:
            _log_view(view)
        decision = behavior_for(view, behaviors)(view, question.tick)
        answer = CraftAnswer(view.object_index, decision.notebook, decision.actions)
        if debug:
            _log_answer(answer)
        answers.append(answer)
    return Answer(question.tick, tuple(answers))


def _log_view(view: CraftView) -> None:
    logger.debug(
        "think.view object=%d signature=%d type=%d group=%d order=%d slot=%d "
        "activity=%d team=%d skill=%d interval=%d throttle=%d x=%d y=%d z=%d "
        'yaw=%d pitch=%d roll=%d speed=%d seed=%d notebook="%s"',
        view.object_index,
        view.object_signature,
        view.object_type,
        view.flight_group,
        view.order_code,
        view.order_slot,
        view.activity,
        view.team,
        view.skill,
        view.think_interval,
        view.throttle,
        view.x,
        view.y,
        view.z,
        view.yaw,
        view.pitch,
        view.roll,
        view.speed,
        view.random_seed,
        view.notebook.hex(),
    )


def _log_answer(answer: CraftAnswer) -> None:
    actions = answer.actions
    steer = actions.steer
    # The bits name the action kinds sent, 1 << kind, as the engine logs them.
    bits = (
        (steer is not None) << 1
        | (actions.throttle is not None) << 2
        | (actions.activity is not None) << 3
        | (actions.think_interval is not None) << 4
    )
    logger.debug(
        "think.answer object=%d actions=%d yaw=%d pitch=%d roll=%d throttle=%d "
        'activity=%d interval=%d notebook="%s"',
        answer.object_index,
        bits,
        steer.yaw if steer else 0,
        steer.pitch if steer else 0,
        steer.roll if steer else 0,
        actions.throttle or 0,
        actions.activity or 0,
        actions.think_interval or 0,
        answer.notebook.hex(),
    )

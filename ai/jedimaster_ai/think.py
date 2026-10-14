"""Think: the answer to one question, decided craft by craft.

Purpose:
    The program's one decision. For each craft in the question, in order,
    ``progress`` reads its books and either sends it home
    (``behaviors.home`` decides) or lets the behavior its order code picks
    decide, adding any move of its order slot (``behaviors.arrival`` while
    the craft arrives from hyperspace; out of a hangar its order decides at
    once and the leaving mark ends), it steers clear of a bulky craft in its
    way (``behaviors.avoidance``), its turrets are aimed
    (``behaviors.turrets``), a starfighter answers a homing shot closing
    on it with chaff or a flare (``behaviors.countermeasures``), and the
    decisions become the answer.

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

from . import progress
from .behaviors import arrival
from .behaviors import avoidance
from .behaviors import Behavior
from .behaviors import countermeasures
from .behaviors import home
from .behaviors import turrets
from .orders import behavior_for
from .orders import BEHAVIORS
from .world import Answer
from .world import Contact
from .world import CraftAnswer
from .world import CraftView
from .world import HOLD_FIRE
from .world import NO_OBJECT
from .world import Question
from .world import WAYPOINTS

logger = logging.getLogger(__name__)


def think(
    question: Question, behaviors: t.Mapping[int, Behavior] = BEHAVIORS
) -> Answer:
    """Return the answer to a question; ``behaviors`` maps order codes."""
    debug = logger.isEnabledFor(logging.DEBUG)
    if debug:
        logger.debug(
            "think.question tick=%d craft=%d contacts=%d",
            question.tick,
            len(question.craft),
            len(question.contacts),
        )
        for contact in question.contacts:
            _log_contact(contact)
    answers = []
    for view in question.craft:
        if debug:
            _log_view(view)
        if arrival.is_arriving(view):
            decision = arrival.think(view, question)
        else:
            step = progress.decide(view)
            behavior = home.think if step.home else behavior_for(view, behaviors)
            decision = progress.with_move(behavior(view, question), step.move)
            if arrival.is_leaving_hangar(view):
                decision = arrival.leave_hangar(view, decision)
            decision = avoidance.steer_clear(view, question, decision)
        decision = turrets.defend(view, question, decision)
        decision = countermeasures.answer(view, question, decision)
        answer = CraftAnswer(view.object_index, decision.notebook, decision.actions)
        if debug:
            _log_answer(answer)
        answers.append(answer)
    return Answer(question.tick, tuple(answers))


def _log_contact(contact: Contact) -> None:
    logger.debug(
        "think.contact object=%d signature=%d type=%d genus=%d team=%d group=%d "
        "kind=%d flags=%d x=%d y=%d z=%d yaw=%d pitch=%d speed=%d target=%d "
        "attacker=%d front=%d rear=%d hull_damage=%d hull=%d activity=%d size=%d",
        contact.object_index,
        contact.object_signature,
        contact.object_type,
        contact.genus,
        contact.team,
        contact.flight_group,
        contact.object_kind,
        contact.flags,
        contact.x,
        contact.y,
        contact.z,
        contact.yaw,
        contact.pitch,
        contact.speed,
        contact.target,
        contact.last_attacker,
        contact.front_shield,
        contact.rear_shield,
        contact.hull_damage,
        contact.hull_strength,
        contact.activity,
        contact.size,
    )


def _log_view(view: CraftView) -> None:
    logger.debug(
        "think.view object=%d signature=%d type=%d group=%d order=%d slot=%d "
        "activity=%d team=%d skill=%d interval=%d throttle=%d x=%d y=%d z=%d "
        "yaw=%d pitch=%d roll=%d speed=%d seed=%d targets=%d turrets=%d "
        "cannons=%d launchers=%d cannon_first=%d cannon_second=%d warhead_first=%d warhead_second=%d "
        "left_first=%d left_second=%d cooldown_first=%d cooldown_second=%d "
        'enemies=%d leader=%d codes="%s" states="%s" order_throttle=%d '
        "variable_first=%d variable_second=%d order_speed=%d full_speed=%d books=%d "
        "mothership=%d outside_x=%d outside_y=%d outside_z=%d inside_x=%d "
        "inside_y=%d inside_z=%d home_x=%d home_y=%d home_z=%d "
        "countermeasure=%d countermeasures_left=%d countermeasure_working=%d "
        "chaff_seconds=%d countermeasure_cooldown=%d threats=%d waypoints=%d "
        'notebook="%s"',
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
        view.order_target_mask,
        view.turret_slot_mask,
        view.cannon_group_count,
        view.warhead_launcher_count,
        view.cannon_type[0],
        view.cannon_type[1],
        view.warhead_type[0],
        view.warhead_type[1],
        view.warheads_left[0],
        view.warheads_left[1],
        view.launcher_cooldown[0],
        view.launcher_cooldown[1],
        view.enemy_mask,
        view.leader,
        bytes(view.order_codes).hex(),
        bytes(view.order_states).hex(),
        view.order_throttle,
        view.order_variable1,
        view.order_variable2,
        view.order_speed,
        view.full_speed,
        view.books,
        view.mothership,
        view.outside_hangar[0],
        view.outside_hangar[1],
        view.outside_hangar[2],
        view.inside_hangar[0],
        view.inside_hangar[1],
        view.inside_hangar[2],
        view.home_point[0],
        view.home_point[1],
        view.home_point[2],
        view.countermeasure,
        view.countermeasures_left,
        view.countermeasure_working,
        view.chaff_seconds,
        view.countermeasure_cooldown,
        view.threat_count,
        view.waypoint_mask,
        view.notebook.hex(),
    )
    for threat in view.threats:
        logger.debug(
            "think.threat object=%d threat=%d type=%d decoyed=%d x=%d y=%d z=%d",
            view.object_index,
            threat.object_index,
            threat.object_type,
            int(threat.decoyed),
            threat.x,
            threat.y,
            threat.z,
        )
    for number in range(WAYPOINTS):
        if view.waypoint_on(number):
            logger.debug(
                "think.waypoint object=%d waypoint=%d x=%d y=%d z=%d",
                view.object_index,
                number,
                view.waypoint_x[number],
                view.waypoint_y[number],
                view.waypoint_z[number],
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
        | (actions.target is not None) << 5
        | (actions.lasers is not None) << 6
        | (actions.warhead is not None) << 7
        | (actions.turrets is not None) << 8
        | (actions.arrival is not None) << 9
        | (actions.order is not None) << 10
        | actions.depart << 11
        | actions.dock << 12
        | actions.countermeasure << 13
    )
    first, second = actions.lasers or (HOLD_FIRE, HOLD_FIRE)
    warhead = actions.warhead
    arrived = actions.arrival
    order = actions.order
    logger.debug(
        "think.answer object=%d actions=%d yaw=%d pitch=%d roll=%d throttle=%d "
        "activity=%d interval=%d target=%d mode_first=%d burst_first=%d mode_second=%d burst_second=%d "
        "launcher=%d paired=%d turrets=%d arriving=%d speed=%d order_slot=%d "
        'order_state=%d notebook="%s"',
        answer.object_index,
        bits,
        steer.yaw if steer else 0,
        steer.pitch if steer else 0,
        steer.roll if steer else 0,
        actions.throttle or 0,
        actions.activity or 0,
        actions.think_interval or 0,
        NO_OBJECT if actions.target is None else actions.target,
        first.mode,
        first.burst,
        second.mode,
        second.burst,
        -1 if warhead is None else warhead.launcher,
        0 if warhead is None else int(warhead.paired),
        len(actions.turrets or ()),
        0 if arrived is None else int(arrived.arriving),
        0 if arrived is None else arrived.speed,
        -1 if order is None else order.slot,
        0 if order is None else order.state,
        answer.notebook.hex(),
    )
    for turret in actions.turrets or ():
        logger.debug(
            "think.turret object=%d slot=%d target=%d",
            answer.object_index,
            turret.slot,
            turret.target,
        )

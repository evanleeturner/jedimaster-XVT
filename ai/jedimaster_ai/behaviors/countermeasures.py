"""Countermeasures: a starfighter answers the homing shots closing on it.

Purpose:
    After a craft's order behavior, avoidance and turrets decide, a
    starfighter carrying chaff or flares answers a homing shot aimed at it,
    whatever its order and whatever it is doing, going home included. In
    1997 only a plan that carried a defending order answered: Go Home's,
    Circle and Evade's and the fighting orders' did, but a craft on Circle,
    on Wait or switched to the plain fly-home plan never answered and was
    left a sitting duck. The rule is the 1997 AI's, from the end
    of its turret order (``paifight_gunnerselfdefenseorder``, deleted with
    the 1997 AI): the first shot in the engine's slot order within the
    craft's threat range is the one answered. The range is THREAT_RANGE by
    the craft's skill tier, three times that for a concussion missile. Chaff
    launches when none is running; a flare launches when no countermeasure
    homes on that shot yet and the cooldown has run out. The engine launches
    (``ai_countermeasures_launch``).

Invariants:
    - Only a starfighter answers, and only with a countermeasure the view
      shows ready (``CraftView.countermeasure_ready``).
    - Only the first shot within range is weighed, as in 1997: a shot
      further down the list waits for the next think, even when the first is
      already decoyed.
    - Distances are exact, as everywhere in the program; 1997 took a rough
      distance.
    - The decision's notebook and its other actions are never changed.
"""

from __future__ import annotations

import dataclasses
import logging

from .. import geometry
from .. import tactics
from ..world import Countermeasure
from ..world import CraftView
from ..world import Question
from ..world import Threat
from . import Decision

logger = logging.getLogger(__name__)

# How close a homing shot comes before a craft answers it, by skill tier, 0
# to 4, in world units: to tier 2 the 1997 AI's
# g_ai_warhead_threat_range_by_skill, then the Sith ranks', a step farther
# each.
THREAT_RANGE = (0x800, 0x1000, 0x1800, 0x2000, 0x2800)
# Concussion missiles are answered from three times as far.
CONCUSSION_RANGE_FACTOR = 3
# The engine's object types of the concussion missiles (laser.h).
CONCUSSION_MISSILE = 144
ADVANCED_CONCUSSION_MISSILE = 149
_CONCUSSION_TYPES = (CONCUSSION_MISSILE, ADVANCED_CONCUSSION_MISSILE)


def answer(view: CraftView, question: Question, decision: Decision) -> Decision:
    """Return the decision with a countermeasure launched against the first
    homing shot within range, when the craft is a starfighter whose
    countermeasure is ready and the shot calls for one."""
    if not view.threats or not view.countermeasure_ready:
        return decision
    own = tactics.own_contact(view, question)
    if own is None or own.genus != tactics.GENUS_STARFIGHTER:
        return decision
    reach = threat_range(view)
    for threat in view.threats:
        distance = geometry.distance(
            threat.x - view.x, threat.y - view.y, threat.z - view.z
        )
        limit = reach * (
            CONCUSSION_RANGE_FACTOR if threat.object_type in _CONCUSSION_TYPES else 1
        )
        if distance >= limit:
            continue
        launch = _calls_for_one(view, threat)
        logger.debug(
            "countermeasures.threat object=%d threat=%d type=%d distance=%d "
            "range=%d kind=%d launch=%d",
            view.object_index,
            threat.object_index,
            threat.object_type,
            distance,
            limit,
            view.countermeasure,
            int(launch),
        )
        if not launch:
            return decision
        actions = dataclasses.replace(decision.actions, countermeasure=True)
        return dataclasses.replace(decision, actions=actions)
    return decision


def threat_range(view: CraftView) -> int:
    """Return how close a homing shot other than a concussion missile comes
    before the craft answers it."""
    return THREAT_RANGE[tactics.skill_tier(view)]


def _calls_for_one(view: CraftView, threat: Threat) -> bool:
    """Whether the shot calls for a launch: a flare only for a shot no
    countermeasure homes on yet; chaff, ready only when none runs, always."""
    return view.countermeasure == Countermeasure.CHAFF or not threat.decoyed

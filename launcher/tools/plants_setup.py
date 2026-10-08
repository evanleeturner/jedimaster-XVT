"""The planted faults of the lobby's setup document and its schema.

Purpose:
    Break one piece of ``jedimaster/setup.py`` or ``schema/setup.schema.json``
    per plant.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Each plant's text is exactly as it was when it lived in
      ``plant_faults.py``; ids are unique across all tables.
    - Each ``old`` text occurs exactly once in its file (the setup module, the setup schema).

Call:
    ``from plants_setup import SETUP_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

S = "jedimaster/setup.py"
SS = "schema/setup.schema.json"
T_SET = "tests/test_setup.py::"

SETUP_PLANTS: list[Plant] = [
    # The lobby's setup document: jedimaster/setup.py and its schema.
    Plant(
        "setup-default-limit",
        S,
        "MISSION_TIME_LIMIT_DEFAULT = 255",
        "MISSION_TIME_LIMIT_DEFAULT = 254",
        (T_SET + "test_engine_values",),
    ),
    Plant(
        "setup-ping-fingerprinted",
        S,
        'PLAYER_UNFINGERPRINTED = ("ready", "ping_ms")',
        'PLAYER_UNFINGERPRINTED = ("ready",)',
        (T_SET + "test_fingerprint_ignores_order_revision_ready_ping_and_launch",),
    ),
    Plant(
        "setup-build-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "build")',
        (T_SET + "test_fingerprint_changes_with_the_battle",),
    ),
    Plant(
        "setup-easy-cheat-value",
        S,
        '"easy_cheat": 3}',
        '"easy_cheat": 2}',
        (T_SET + "test_engine_value_edges",),
    ),
    Plant(
        "setup-battle-length-value",
        S,
        "BATTLE_LENGTH_VALUES = {2: 0, 3: 1, 4: 2}",
        "BATTLE_LENGTH_VALUES = {2: 0, 3: 2, 4: 2}",
        (T_SET + "test_engine_values",),
    ),
    Plant(
        "setup-keys-unsorted",
        S,
        "sort_keys=True",
        "sort_keys=False",
        (T_SET + "test_fingerprint_ignores_order_revision_ready_ping_and_launch",),
    ),
    Plant(
        "setup-shallow-view",
        S,
        "view = copy.deepcopy(document)",
        "view = copy.copy(document)",
        (T_SET + "test_fingerprint_does_not_change_the_document",),
    ),
    Plant(
        "setup-boolean-inverted",
        S,
        "int(bool(settings[name]))",
        "int(not settings[name])",
        (T_SET + "test_engine_values",),
    ),
    Plant(
        "setup-last-team-none",
        S,
        'LAST_TEAM_TIME_LIMIT_NONE if last == "none"',
        '1 if last == "none"',
        (T_SET + "test_engine_values",),
    ),
    Plant(
        "setup-unknown-name-accepted",
        S,
        "    if value not in table:\n        raise ValueError",
        "    if False:\n        raise ValueError",
        (T_SET + "test_engine_values_refuse_an_unknown_name",),
    ),
    Plant(
        "setup-schema-mission-limit",
        SS,
        '"minimum": 1, "maximum": 20}',
        '"minimum": 1, "maximum": 21}',
        (T_SET + "test_out_of_range_values_are_refused",),
    ),
    Plant(
        "setup-schema-last-team-limit",
        SS,
        '"minimum": 1, "maximum": 10}',
        '"minimum": 1, "maximum": 20}',
        (T_SET + "test_out_of_range_values_are_refused",),
    ),
    Plant(
        "setup-schema-ninth-player",
        SS,
        '"maxItems": 8,\n      "items": {\n        "type": "object"',
        '"maxItems": 9,\n      "items": {\n        "type": "object"',
        (T_SET + "test_unknown_setting_and_ninth_player_are_refused",),
    ),
    Plant(
        "setup-schema-update-rate",
        SS,
        '"enum": [4, 6, 8]',
        '"enum": [4, 5, 6, 8]',
        (T_SET + "test_out_of_range_values_are_refused",),
    ),
    Plant(
        "setup-schema-path-slashes",
        SS,
        '"pattern": "^[^/\\\\\\\\][^\\\\\\\\]*\\\\.(tie|TIE)$"',
        '"pattern": "^.*$"',
        (T_SET + "test_out_of_range_values_are_refused",),
    ),
    Plant(
        "setup-schema-invalid",
        SS,
        '  "type": "object",\n  "additionalProperties": false,\n  "required": ["schema_version"',
        '  "type": "objectt",\n  "additionalProperties": false,\n  "required": ["schema_version"',
        (T_SET + "test_schema_is_a_valid_schema",),
    ),
    Plant(
        "setup-schema-no-medium",
        SS,
        '"enum": ["easy", "medium", "hard", "easy_cheat"]',
        '"enum": ["easy", "hard", "easy_cheat"]',
        (T_SET + "test_sample_is_valid",),
    ),
    Plant(
        "setup-schema-zero-minutes",
        SS,
        '"minimum": 1, "maximum": 20}',
        '"minimum": 0, "maximum": 20}',
        (T_SET + "test_out_of_range_values_are_refused[path1-0]",),
    ),
    Plant(
        "setup-schema-last-team-default",
        SS,
        '"oneOf": [{"const": "none"}',
        '"oneOf": [{"enum": ["none", "default"]}',
        (T_SET + "test_out_of_range_values_are_refused[path3-default]",),
    ),
    Plant(
        "setup-schema-one-win",
        SS,
        '"enum": [2, 3, 4]',
        '"enum": [1, 2, 3, 4]',
        (T_SET + "test_out_of_range_values_are_refused[path5-1]",),
    ),
    Plant(
        "setup-schema-insane",
        SS,
        '"enum": ["easy", "medium", "hard", "easy_cheat"]',
        '"enum": ["easy", "medium", "hard", "easy_cheat", "insane"]',
        (T_SET + "test_out_of_range_values_are_refused[path6-insane]",),
    ),
    Plant(
        "setup-schema-seed-wide",
        SS,
        '"maximum": 4294967295',
        '"maximum": 4294967296',
        (T_SET + "test_out_of_range_values_are_refused[path7-4294967296]",),
    ),
    Plant(
        "setup-schema-rules-1998",
        SS,
        '"enum": ["1997", "fixed"]',
        '"enum": ["1997", "1998", "fixed"]',
        (T_SET + "test_out_of_range_values_are_refused[path8-1998]",),
    ),
    Plant(
        "setup-schema-version-2",
        SS,
        '"schema_version": {"const": 1}',
        '"schema_version": {"enum": [1, 2]}',
        (T_SET + "test_out_of_range_values_are_refused[path10-2]",),
    ),
    Plant(
        "setup-settings-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "settings")',
        (
            T_SET + "test_fingerprint_changes_with_the_battle[path0-False]",
            T_SET + "test_fingerprint_changes_with_the_battle[path1-4243]",
        ),
    ),
    Plant(
        "setup-engine-options-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "engine_options")',
        (T_SET + "test_fingerprint_changes_with_the_battle[path2-fixed]",),
    ),
    Plant(
        "setup-mission-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "mission")',
        (T_SET + "test_fingerprint_changes_with_the_battle[path3-",),
    ),
    Plant(
        "setup-teams-unfingerprinted",
        S,
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch")',
        'TOP_LEVEL_UNFINGERPRINTED = ("revision", "launch", "teams")',
        (T_SET + "test_fingerprint_changes_with_the_battle[path6-",),
    ),
    Plant(
        "setup-flight-group-unfingerprinted",
        S,
        'PLAYER_UNFINGERPRINTED = ("ready", "ping_ms")',
        'PLAYER_UNFINGERPRINTED = ("ready", "ping_ms", "flight_group")',
        (T_SET + "test_fingerprint_changes_with_the_battle[path5-2]",),
    ),
    Plant(
        "setup-none-limit",
        S,
        "MISSION_TIME_LIMIT_NONE = 0",
        "MISSION_TIME_LIMIT_NONE = 1",
        (T_SET + "test_engine_value_edges[mission_time_limit-none-0]",),
    ),
    Plant(
        "setup-minutes-wrapped",
        S,
        'values["mission_time_limit"] = int(limit)',
        'values["mission_time_limit"] = int(limit) % 20',
        (T_SET + "test_engine_value_edges[mission_time_limit-20-20]",),
    ),
    Plant(
        "setup-last-team-wrapped",
        S,
        '"none" else int(last)',
        '"none" else int(last) % 10',
        (T_SET + "test_engine_value_edges[last_team_time_limit-10-10]",),
    ),
    Plant(
        "setup-four-wins",
        S,
        "BATTLE_LENGTH_VALUES = {2: 0, 3: 1, 4: 2}",
        "BATTLE_LENGTH_VALUES = {2: 0, 3: 1, 4: 1}",
        (T_SET + "test_engine_value_edges[battle_length-4-2]",),
    ),
    Plant(
        "setup-host-only",
        S,
        '"host_only": 2}',
        '"host_only": 1}',
        (T_SET + "test_engine_value_edges[craft_selection-host_only-2]",),
    ),
    Plant(
        "setup-unlimited",
        S,
        '"unlimited": 2}',
        '"unlimited": 1}',
        (T_SET + "test_engine_value_edges[craft_waves-unlimited-2]",),
    ),
    Plant(
        "setup-favor-rebel",
        S,
        '"favor_rebel": 3,',
        '"favor_rebel": 2,',
        (T_SET + "test_engine_value_edges[combat_balance-favor_rebel-3]",),
    ),
    Plant(
        "setup-continue",
        S,
        '{"restart": 0, "continue": 1}',
        '{"restart": 0, "continue": 0}',
        (T_SET + "test_engine_value_edges[continue_sequence-continue-1]",),
    ),
]

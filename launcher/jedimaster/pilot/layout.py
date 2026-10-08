"""The layout of the pilot records: every struct of the two files, as data.

Purpose:
    Hold, as data, the layout the game's program uses for a pilot: the
    full record of a ``.pl2`` file (``pilot_data``, 296,238 bytes), the
    base record of a ``.plt`` file (``pilot_xvt_record``, 253,754 bytes)
    and every struct they are made of, each member's name, offset, type,
    dimensions and size. Neither file names its fields: this is the layout
    of the game's program, as printed by the engine's pilot_dump tool,
    copied here from that printout. The member names are the game
    program's. With the strings.txt tables of ``jedimaster.text.tables``,
    the icon tables of ``jedimaster.icons.tables`` and the text colors of
    ``jedimaster.fonts.colors``, it is the only game data in this
    package's code.

Flow:
    ``STRUCTS`` lists the structs in the printout's order; ``struct``
    looks one up by name; ``element_size`` and ``count`` give an array
    member's element size and element count; nothing here reads a file.

Invariants:
    - Records are packed with no gaps: a struct's members lie end to end
      from offset 0 and fill its size; an array's elements lie end to end,
      the last index running fastest; numbers are little-endian.
    - A member's ``type`` is ``i32`` (signed 32-bit), ``u32`` (unsigned
      32-bit), ``u8`` (a byte), ``char`` (a byte of text) or the name of
      another struct of ``STRUCTS``; ``dims`` is empty for one value;
      ``size`` covers the whole array.

Call:
    ``struct(FULL_RECORD).size`` -> ``296238``
"""

from __future__ import annotations

import logging
from math import prod
from typing import NamedTuple

logger = logging.getLogger(__name__)

I32 = "i32"
U32 = "u32"
U8 = "u8"
CHAR = "char"
NUMBER_SIZES = {I32: 4, U32: 4, U8: 1, CHAR: 1}
"""The plain types and the bytes of one value of each."""

FULL_RECORD = "pilot_data"
"""The full record: a ``.pl2`` file, and what the game holds after a load."""
BASE_RECORD = "pilot_xvt_record"
"""The base game's record: a ``.plt`` file."""


class Member(NamedTuple):
    """One member of a struct: name, offset, type, dimensions, whole size."""

    name: str
    offset: int
    type: str
    dims: tuple[int, ...]
    size: int


class Struct(NamedTuple):
    """One struct: its name, its size in bytes and its members in order."""

    name: str
    size: int
    members: tuple[Member, ...]


STRUCTS: tuple[Struct, ...] = (
    Struct(
        "pilot_network_player",
        88,
        (
            Member("formal_name", 0, "char", (14,), 14),
            Member("friendly_name", 14, "char", (14,), 14),
            Member("flight_group_id", 28, "i32", (), 4),
            Member("direct_play_id", 32, "i32", (), 4),
            Member("rating", 36, "i32", (), 4),
            Member("total_score", 40, "i32", (), 4),
            Member("kills", 44, "i32", (), 4),
            Member("kills_shared", 48, "i32", (), 4),
            Member("craft_inspected", 52, "i32", (), 4),
            Member("kills_assist", 56, "i32", (), 4),
            Member("total_losses", 60, "i32", (), 4),
            Member("craft_id", 64, "i32", (), 4),
            Member("craft_option", 68, "i32", (), 4),
            Member("warhead_option", 72, "i32", (), 4),
            Member("beam_option", 76, "i32", (), 4),
            Member("countermeasure_option", 80, "i32", (), 4),
            Member("has_left", 84, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_team",
        28,
        (
            Member("mission_score", 0, "i32", (), 4),
            Member("is_mission_completed", 4, "i32", (), 4),
            Member("unknown08", 8, "i32", (), 4),
            Member("mission_time", 12, "i32", (), 4),
            Member("kills", 16, "i32", (), 4),
            Member("kills_shared", 20, "i32", (), 4),
            Member("losses", 24, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_mission",
        36,
        (
            Member("number_times_flown", 0, "i32", (), 4),
            Member("completed_count", 4, "i32", (), 4),
            Member("failed_count", 8, "i32", (), 4),
            Member("best_score", 12, "i32", (), 4),
            Member("best_time", 16, "i32", (), 4),
            Member("best_placement", 20, "i32", (), 4),
            Member("award_level", 24, "i32", (), 4),
            Member("best_margin", 28, "u32", (), 4),
            Member("field20", 32, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_multiplayer_mission",
        48,
        (
            Member("number_times_flown", 0, "i32", (), 4),
            Member("first_place_count", 4, "i32", (), 4),
            Member("second_place_count", 8, "i32", (), 4),
            Member("third_place_count", 12, "i32", (), 4),
            Member("completed_count", 16, "i32", (), 4),
            Member("failed_count", 20, "i32", (), 4),
            Member("best_score", 24, "i32", (), 4),
            Member("best_time", 28, "i32", (), 4),
            Member("best_placement", 32, "i32", (), 4),
            Member("award_level", 36, "i32", (), 4),
            Member("best_margin", 40, "u32", (), 4),
            Member("field2c", 44, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_tournament",
        40,
        (
            Member("attempt_count", 0, "i32", (), 4),
            Member("completed_count", 4, "i32", (), 4),
            Member("first_place_count", 8, "i32", (), 4),
            Member("second_place_count", 12, "i32", (), 4),
            Member("third_place_count", 16, "i32", (), 4),
            Member("best_score", 20, "i32", (), 4),
            Member("best_placement", 24, "i32", (), 4),
            Member("award_level", 28, "i32", (), 4),
            Member("best_margin", 32, "u32", (), 4),
            Member("field24", 36, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_multiplayer_tournament",
        44,
        (
            Member("attempt_count", 0, "i32", (), 4),
            Member("completed_count", 4, "i32", (), 4),
            Member("first_place_count", 8, "i32", (), 4),
            Member("second_place_count", 12, "i32", (), 4),
            Member("third_place_count", 16, "i32", (), 4),
            Member("best_score", 20, "i32", (), 4),
            Member("best_placement", 24, "i32", (), 4),
            Member("unused1c", 28, "i32", (), 4),
            Member("award_level", 32, "i32", (), 4),
            Member("best_margin", 36, "u32", (), 4),
            Member("field28", 40, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_battle",
        36,
        (
            Member("attempt_count", 0, "i32", (), 4),
            Member("victory_count", 4, "i32", (), 4),
            Member("defeat_count", 8, "i32", (), 4),
            Member("draw_count", 12, "i32", (), 4),
            Member("best_score", 16, "i32", (), 4),
            Member("unused14", 20, "i32", (), 4),
            Member("award_level", 24, "i32", (), 4),
            Member("best_victory_margin", 28, "u32", (), 4),
            Member("field20", 32, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_multiplayer_battle",
        40,
        (
            Member("attempt_count", 0, "i32", (), 4),
            Member("victory_count", 4, "i32", (), 4),
            Member("defeat_count", 8, "i32", (), 4),
            Member("draw_count", 12, "i32", (), 4),
            Member("best_score", 16, "i32", (), 4),
            Member("unused14", 20, "i32", (), 4),
            Member("unused18", 24, "i32", (), 4),
            Member("award_level", 28, "i32", (), 4),
            Member("best_victory_margin", 32, "u32", (), 4),
            Member("unused24", 36, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_campaign",
        36,
        (
            Member("attempt_count", 0, "i32", (), 4),
            Member("next_mission_index", 4, "i32", (), 4),
            Member("is_finished", 8, "i32", (), 4),
            Member("best_score", 12, "i32", (), 4),
            Member("unused10", 16, "i32", (), 4),
            Member("unused14", 20, "i32", (), 4),
            Member("unused18", 24, "i32", (), 4),
            Member("unused1c", 28, "i32", (), 4),
            Member("unused20", 32, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_campaign_mission",
        32,
        (
            Member("campaign_id", 0, "i32", (), 4),
            Member("number_times_flown", 4, "i32", (), 4),
            Member("award_eligible", 8, "i32", (), 4),
            Member("best_score", 12, "u32", (), 4),
            Member("award_level", 16, "i32", (), 4),
            Member("best_time", 20, "i32", (), 4),
            Member("is_completed", 24, "i32", (), 4),
            Member("unused1c", 28, "i32", (), 4),
        ),
    ),
    Struct(
        "pilot_stats",
        5256,
        (
            Member("total_score_per_mt", 0, "i32", (3,), 12),
            Member("standalone_missions_played_per_mt", 12, "i32", (3,), 12),
            Member("sequence_missions_played_per_mt", 24, "i32", (3,), 12),
            Member("total_kills_per_mt", 36, "i32", (3,), 12),
            Member("total_friendlies_killed_per_mt", 48, "i32", (3,), 12),
            Member("kills_per_craft_per_mt", 60, "i32", (3, 100), 1200),
            Member("kills_shared_per_craft_per_mt", 1260, "i32", (3, 100), 1200),
            Member("kills_assists_per_craft_per_mt", 2460, "i32", (3, 100), 1200),
            Member("kills_full_on_player_rating_per_mt", 3660, "i32", (3, 25), 300),
            Member("kills_shared_on_player_rating_per_mt", 3960, "i32", (3, 25), 300),
            Member("kills_assist_on_player_rating_per_mt", 4260, "i32", (3, 25), 300),
            Member("kills_full_on_ai_rating_per_mt", 4560, "i32", (3, 6), 72),
            Member("kills_shared_on_ai_rating_per_mt", 4632, "i32", (3, 6), 72),
            Member("kills_assist_on_ai_rating_per_mt", 4704, "i32", (3, 6), 72),
            Member("num_special_inspected_per_mt", 4776, "i32", (3,), 12),
            Member("energy_hits_per_mt", 4788, "i32", (3,), 12),
            Member("energy_fired_per_mt", 4800, "i32", (3,), 12),
            Member("warheads_hits_per_mt", 4812, "i32", (3,), 12),
            Member("warheads_fired_per_mt", 4824, "i32", (3,), 12),
            Member("total_craft_losses_per_mt", 4836, "i32", (3,), 12),
            Member("losses_by_collisions_per_mt", 4848, "i32", (3,), 12),
            Member("losses_by_starships_per_mt", 4860, "i32", (3,), 12),
            Member("losses_by_mines_per_mt", 4872, "i32", (3,), 12),
            Member("killed_by_player_rating_per_mt", 4884, "i32", (3, 25), 300),
            Member("killed_by_ai_rating_per_mt", 5184, "i32", (3, 6), 72),
        ),
    ),
    Struct(
        "pilot_faction",
        68064,
        (
            Member("total_missions_played_count", 0, "i32", (), 4),
            Member("team", 4, "i32", (), 4),
            Member("mission_directory_id", 8, "i32", (), 4),
            Member("mission_description_ids", 12, "i32", (6,), 24),
            Member("unused24", 36, "u8", (32,), 32),
            Member("mission_sequence_active", 68, "i32", (), 4),
            Member("saved_mission_description_id", 72, "i32", (), 4),
            Member("melee_plaques", 76, "i32", (6,), 24),
            Member("tournament_trophies", 100, "i32", (6,), 24),
            Member("mission_evaluations", 124, "i32", (6,), 24),
            Member("battle_medallions", 148, "i32", (6,), 24),
            Member("mission_awards", 172, "i32", (4,), 16),
            Member("field_bc", 188, "u8", (16,), 16),
            Member("total_score", 204, "i32", (), 4),
            Member("stats", 208, "pilot_stats", (), 5256),
            Member("field1558", 5464, "u8", (4,), 4),
            Member("sp_training_missions", 5468, "pilot_mission", (100,), 3600),
            Member("sp_melee_missions", 9068, "pilot_mission", (250,), 9000),
            Member("sp_combat_missions", 18068, "pilot_mission", (250,), 9000),
            Member(
                "mp_training_missions", 27068, "pilot_multiplayer_mission", (100,), 4800
            ),
            Member(
                "mp_melee_missions", 31868, "pilot_multiplayer_mission", (250,), 12000
            ),
            Member(
                "mp_combat_missions", 43868, "pilot_multiplayer_mission", (250,), 12000
            ),
            Member("sp_tournaments", 55868, "pilot_tournament", (25,), 1000),
            Member(
                "mp_tournaments", 56868, "pilot_multiplayer_tournament", (25,), 1100
            ),
            Member("sp_battles", 57968, "pilot_battle", (25,), 900),
            Member("mp_battles", 58868, "pilot_multiplayer_battle", (25,), 1000),
            Member("sp_campaigns", 59868, "pilot_campaign", (25,), 900),
            Member("mp_campaigns", 60768, "pilot_campaign", (25,), 900),
            Member("unused_f0e4", 61668, "u8", (24,), 24),
            Member("cd_movie_check_counter", 61692, "u32", (), 4),
            Member(
                "sp_campaign_missions", 61696, "pilot_campaign_mission", (99,), 3168
            ),
            Member("unused_fd60", 64864, "u8", (32,), 32),
            Member(
                "mp_campaign_missions", 64896, "pilot_campaign_mission", (99,), 3168
            ),
        ),
    ),
    Struct(
        "melee_tournament_team_standings",
        20,
        (
            Member("ai_opponent_source_team_and_type_flag", 0, "i32", (), 4),
            Member("total_score", 4, "i32", (), 4),
            Member("first_place_count", 8, "i32", (), 4),
            Member("second_place_count", 12, "i32", (), 4),
            Member("third_place_count", 16, "i32", (), 4),
        ),
    ),
    Struct(
        "melee_tournament_sequence_state",
        256,
        (
            Member("reserved", 0, "i32", (9,), 36),
            Member("current_mission_index", 36, "i32", (), 4),
            Member("mission_count", 40, "i32", (), 4),
            Member("team_standings", 44, "melee_tournament_team_standings", (10,), 200),
            Member("human_player_count", 244, "u32", (), 4),
            Member("participating_team_count", 248, "i32", (), 4),
            Member("ai_boost_first_team", 252, "i32", (), 4),
        ),
    ),
    Struct(
        "battle_sequence_state",
        140,
        (
            Member("current_mission_index", 0, "u32", (), 4),
            Member("current_mission_id", 4, "i32", (), 4),
            Member("victories_needed", 8, "i32", (), 4),
            Member("mission_results", 12, "i32", (10,), 40),
            Member("mission_ordinals", 52, "i32", (10,), 40),
            Member("mission_list_indices", 92, "i32", (10,), 40),
            Member("human_player_count", 132, "u32", (), 4),
            Member("cumulative_score", 136, "i32", (), 4),
        ),
    ),
    Struct(
        "campaign_sequence_state",
        24,
        (
            Member("unused00", 0, "i32", (), 4),
            Member("current_mission_index", 4, "i32", (), 4),
            Member("mission_count", 8, "i32", (), 4),
            Member("last_mission_completed", 12, "i32", (), 4),
            Member("human_player_count", 16, "i32", (), 4),
            Member("cumulative_score", 20, "i32", (), 4),
        ),
    ),
    Struct(
        "battle_continuation",
        160,
        (
            Member("unused00", 0, "i32", (), 4),
            Member("random_seed", 4, "u32", (), 4),
            Member("is_active", 8, "i32", (), 4),
            Member("battle_length_index", 12, "i32", (), 4),
            Member("random_setup", 16, "i32", (), 4),
            Member("sequence_state", 20, "battle_sequence_state", (), 140),
        ),
    ),
    Struct(
        "campaign_continuation",
        40,
        (
            Member("unused00", 0, "i32", (), 4),
            Member("random_seed", 4, "u32", (), 4),
            Member("is_active", 8, "i32", (), 4),
            Member("random_setup", 12, "i32", (), 4),
            Member("sequence_state", 16, "campaign_sequence_state", (), 24),
        ),
    ),
    Struct(
        "pilot_data",
        296238,
        (
            Member("name", 0, "char", (14,), 14),
            Member("total_score", 14, "i32", (), 4),
            Member("local_player_id", 18, "i32", (), 4),
            Member("launch_session_marker", 22, "i32", (), 4),
            Member("is_host", 26, "i32", (), 4),
            Member("num_human_players_last_mission", 30, "u32", (), 4),
            Member("session_mode", 34, "i32", (), 4),
            Member("xvt_record_payload", 38, "u8", (672,), 672),
            Member("team", 710, "i32", (), 4),
            Member("mission_directory_id", 714, "i32", (), 4),
            Member("mission_description_ids", 718, "i32", (6,), 24),
            Member("multiplayer_game_name", 742, "char", (32,), 32),
            Member("multiplayer_host_name", 774, "char", (32,), 32),
            Member("mission_sequence_active", 806, "i32", (), 4),
            Member("saved_mission_description_id", 810, "i32", (), 4),
            Member("current_rating_promo_points", 814, "i32", (), 4),
            Member("current_rating_worse_promo_points", 818, "i32", (), 4),
            Member("promotion_delta", 822, "i32", (), 4),
            Member("next_promotion_percent", 826, "i32", (), 4),
            Member("main_stats", 830, "pilot_stats", (), 5256),
            Member(
                "melee_tournament_sequence_state",
                6086,
                "melee_tournament_sequence_state",
                (),
                256,
            ),
            Member("battle_sequence_state", 6342, "battle_sequence_state", (), 140),
            Member("rating", 6482, "i32", (), 4),
            Member("total_missions_played_count", 6486, "i32", (), 4),
            Member("rating_achieved_on_mission", 6490, "i32", (25,), 100),
            Member("rating_name", 6590, "char", (32,), 32),
            Member("mission_score", 6622, "i32", (), 4),
            Member("kills_full_on_player", 6626, "i32", (8,), 32),
            Member("kills_shared_on_player", 6658, "i32", (8,), 32),
            Member("kills_full_on_flight_group", 6690, "i32", (48,), 192),
            Member("kills_shared_on_flight_group", 6882, "i32", (48,), 192),
            Member("kills_full_from_player", 7074, "i32", (8,), 32),
            Member("kills_shared_from_player", 7106, "i32", (8,), 32),
            Member("kills_full_from_flight_group", 7138, "i32", (48,), 192),
            Member("kills_shared_from_flight_group", 7330, "i32", (48,), 192),
            Member("flight_group_rating", 7522, "i32", (48,), 192),
            Member("last_mission_stats", 7714, "pilot_stats", (), 5256),
            Member("network_players", 12970, "pilot_network_player", (8,), 704),
            Member("teams", 13674, "pilot_team", (10,), 280),
            Member("current_faction_id", 13954, "i32", (), 4),
            Member("faction_statistics", 13958, "pilot_faction", (4,), 272256),
            Member(
                "campaign_sequence_state", 286214, "campaign_sequence_state", (), 24
            ),
            Member(
                "sp_battle_continuations", 286238, "battle_continuation", (25,), 4000
            ),
            Member(
                "mp_battle_continuations", 290238, "battle_continuation", (25,), 4000
            ),
            Member(
                "sp_campaign_continuations",
                294238,
                "campaign_continuation",
                (25,),
                1000,
            ),
            Member(
                "mp_campaign_continuations",
                295238,
                "campaign_continuation",
                (25,),
                1000,
            ),
        ),
    ),
    Struct(
        "pilot_xvt_stats",
        4824,
        (
            Member("total_score_per_mt", 0, "i32", (3,), 12),
            Member("standalone_missions_played_per_mt", 12, "i32", (3,), 12),
            Member("sequence_missions_played_per_mt", 24, "i32", (3,), 12),
            Member("total_kills_per_mt", 36, "i32", (3,), 12),
            Member("total_friendlies_killed_per_mt", 48, "i32", (3,), 12),
            Member("kills_per_craft_per_mt", 60, "i32", (3, 88), 1056),
            Member("kills_shared_per_craft_per_mt", 1116, "i32", (3, 88), 1056),
            Member("kills_assists_per_craft_per_mt", 2172, "i32", (3, 88), 1056),
            Member("kills_full_on_player_rating_per_mt", 3228, "i32", (3, 25), 300),
            Member("kills_shared_on_player_rating_per_mt", 3528, "i32", (3, 25), 300),
            Member("kills_assist_on_player_rating_per_mt", 3828, "i32", (3, 25), 300),
            Member("kills_full_on_ai_rating_per_mt", 4128, "i32", (3, 6), 72),
            Member("kills_shared_on_ai_rating_per_mt", 4200, "i32", (3, 6), 72),
            Member("kills_assist_on_ai_rating_per_mt", 4272, "i32", (3, 6), 72),
            Member("num_special_inspected_per_mt", 4344, "i32", (3,), 12),
            Member("energy_hits_per_mt", 4356, "i32", (3,), 12),
            Member("energy_fired_per_mt", 4368, "i32", (3,), 12),
            Member("warheads_hits_per_mt", 4380, "i32", (3,), 12),
            Member("warheads_fired_per_mt", 4392, "i32", (3,), 12),
            Member("total_craft_losses_per_mt", 4404, "i32", (3,), 12),
            Member("losses_by_collisions_per_mt", 4416, "i32", (3,), 12),
            Member("losses_by_starships_per_mt", 4428, "i32", (3,), 12),
            Member("losses_by_mines_per_mt", 4440, "i32", (3,), 12),
            Member("killed_by_player_rating_per_mt", 4452, "i32", (3, 25), 300),
            Member("killed_by_ai_rating_per_mt", 4752, "i32", (3, 6), 72),
        ),
    ),
    Struct(
        "pilot_xvt_faction",
        59428,
        (
            Member("total_missions_played_count", 0, "i32", (), 4),
            Member("selection_state", 4, "u8", (68,), 68),
            Member("melee_plaques", 72, "i32", (6,), 24),
            Member("tournament_trophies", 96, "i32", (6,), 24),
            Member("mission_evaluations", 120, "i32", (6,), 24),
            Member("battle_medallions", 144, "i32", (6,), 24),
            Member("mission_awards", 168, "i32", (4,), 16),
            Member("field_bc", 184, "u8", (16,), 16),
            Member("total_score", 200, "i32", (), 4),
            Member("stats", 204, "pilot_xvt_stats", (), 4824),
            Member("sp_training_data", 5028, "u8", (3600,), 3600),
            Member("sp_melee_data", 8628, "u8", (9000,), 9000),
            Member("sp_combat_data", 17628, "u8", (9000,), 9000),
            Member("mp_training_data", 26628, "u8", (4800,), 4800),
            Member("mp_melee_data", 31428, "u8", (12000,), 12000),
            Member("mp_combat_data", 43428, "u8", (12000,), 12000),
            Member("sp_tournament_data", 55428, "u8", (1000,), 1000),
            Member("mp_tournament_data", 56428, "u8", (1100,), 1100),
            Member("sp_battle_data", 57528, "u8", (900,), 900),
            Member("mp_battle_data", 58428, "u8", (1000,), 1000),
        ),
    ),
    Struct(
        "pilot_xvt_record",
        253754,
        (
            Member("name", 0, "char", (14,), 14),
            Member("total_score", 14, "i32", (), 4),
            Member("local_player_id", 18, "i32", (), 4),
            Member("launch_session_marker", 22, "i32", (), 4),
            Member("is_host", 26, "i32", (), 4),
            Member("num_human_players_last_mission", 30, "u32", (), 4),
            Member("session_mode", 34, "i32", (), 4),
            Member("xvt_record_combat_payload", 38, "u8", (320,), 320),
            Member("xvt_record_identity_payload", 358, "u8", (32,), 32),
            Member("xvt_record_object_payload", 390, "u8", (320,), 320),
            Member("legacy_rating_state", 710, "u8", (100,), 100),
            Member("current_rating_promo_points", 810, "i32", (), 4),
            Member("current_rating_worse_promo_points", 814, "i32", (), 4),
            Member("promotion_delta", 818, "i32", (), 4),
            Member("next_promotion_percent", 822, "i32", (), 4),
            Member("main_stats", 826, "pilot_xvt_stats", (), 4824),
            Member("mission_sequence_state", 5650, "u8", (3348,), 3348),
            Member("rating", 8998, "i32", (), 4),
            Member("total_missions_played_count", 9002, "i32", (), 4),
            Member("rating_achieved_on_mission", 9006, "i32", (25,), 100),
            Member("rating_name", 9106, "char", (32,), 32),
            Member("mission_score", 9138, "i32", (), 4),
            Member("kills_full_on_player", 9142, "i32", (8,), 32),
            Member("kills_shared_on_player", 9174, "i32", (8,), 32),
            Member("kills_full_on_flight_group", 9206, "i32", (48,), 192),
            Member("kills_shared_on_flight_group", 9398, "i32", (48,), 192),
            Member("kills_full_from_player", 9590, "i32", (8,), 32),
            Member("kills_shared_from_player", 9622, "i32", (8,), 32),
            Member("kills_full_from_flight_group", 9654, "i32", (48,), 192),
            Member("kills_shared_from_flight_group", 9846, "i32", (48,), 192),
            Member("flight_group_rating", 10038, "i32", (48,), 192),
            Member("last_mission_stats", 10230, "pilot_xvt_stats", (), 4824),
            Member("network_players", 15054, "pilot_network_player", (8,), 704),
            Member("teams", 15758, "pilot_team", (10,), 280),
            Member("current_faction_id", 16038, "i32", (), 4),
            Member("faction_statistics", 16042, "pilot_xvt_faction", (4,), 237712),
        ),
    ),
)
"""Every struct of the two records, in the printout's order."""

_BY_NAME = {s.name: s for s in STRUCTS}


def struct(name: str) -> Struct:
    """Return the struct named ``name``.

    Raises ``KeyError`` for a name that is not a struct of ``STRUCTS``
    (a plain type included). Does not log.
    """
    return _BY_NAME[name]


def is_struct(type_name: str) -> bool:
    """Return True when ``type_name`` names a struct, False for a plain type.

    Does not check that a name it does not know is a plain type.
    """
    return type_name in _BY_NAME


def count(member: Member) -> int:
    """Return how many elements ``member`` holds: 1 for one value.

    The product of its dimensions. Does not check ``size``.
    """
    return prod(member.dims)


def element_size(member: Member) -> int:
    """Return the bytes of one element of ``member``.

    A plain type's value size, or the struct's size. Raises ``KeyError``
    for a type that is neither. Does not check ``size``.
    """
    if member.type in NUMBER_SIZES:
        return NUMBER_SIZES[member.type]
    return struct(member.type).size

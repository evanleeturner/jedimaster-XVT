# Unit tests. Each test is a plain C program: CTest runs it, and a nonzero
# exit is a failure. Build and run them with:
#
#   cmake -S . -B build -DXVT_BUILD_TESTS=ON
#   cmake --build build --target xvt_tests
#   ctest --test-dir build --output-on-failure
#
# On GCC and Clang every test program builds with AddressSanitizer and
# UndefinedBehaviorSanitizer, whatever XVT_ENABLE_ASAN and XVT_ENABLE_UBSAN
# say: a test that passes while undefined behavior runs proves little. The
# three harness_catches_* programs fail on purpose, to prove the harness and
# both sanitizers still report what they should.

add_custom_target(xvt_tests)

set(XVT_TEST_SANITIZE_OPTIONS -fsanitize=address,undefined -fno-sanitize=alignment)

# The arguments after the name are the program's source files.
function(xvt_add_test_program name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -fno-omit-frame-pointer
            -g
            ${XVT_TEST_SANITIZE_OPTIONS}
        )
        target_link_options(${name} PRIVATE ${XVT_TEST_SANITIZE_OPTIONS})
        target_link_libraries(${name} PRIVATE m)
    endif()
    add_dependencies(xvt_tests ${name})
endfunction()

# A test of one engine module: the arguments after the name are the test's source and the module's own
# sources, compiled into the test program so the sanitizers watch the code under test. Everything else
# the module reaches (the recovered game's globals and functions, Aeron) comes from the xvt_core library.
# The library's copy of the module is never linked in: the test program already defines its symbols.
# The test sets the game state it needs itself; no game data is read.
function(xvt_add_engine_test name)
    xvt_add_test_program(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE xvt_core)
endfunction()

# A check that shows a module breaking its own written promise is kept, not deleted. The test program runs
# it only when started as "<program> known-failure <check>", and that run is registered as expected to
# fail, so the rest of the suite stays green while the fault stands. Once the module is fixed the run
# passes, CTest reports it as a failure, and the check moves back into the program's main run. A program
# given a check name it does not know returns 0, so a misspelled name also shows up as a failure.
function(xvt_add_known_failure program check)
    add_test(NAME ${program}_known_failure_${check} COMMAND ${program} known-failure ${check})
    set_tests_properties(${program}_known_failure_${check} PROPERTIES WILL_FAIL TRUE)
endfunction()

xvt_add_test_program(test_harness test_harness.c)
add_test(NAME harness COMMAND test_harness)

# The log header is tested against a stub of Aeron's log funnel, with NDEBUG defined as a release
# build defines it, to show DEBUG lines still switch on at run time.
xvt_add_test_program(test_log test_log.c "${PROJECT_SOURCE_DIR}/src/xvt_runtime/log/log.c")
target_include_directories(test_log PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/aeron/include")
target_compile_definitions(test_log PRIVATE NDEBUG)
add_test(NAME log COMMAND test_log)

xvt_add_engine_test(test_world_state test_world_state.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/world_state.c"
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/world_checksum.c")
add_test(NAME world_state COMMAND test_world_state)

# The computer pilots' context, range tests and plans: src/xvt/flight/ai/pai.c.
# The plan file checks write their files into a temporary folder made with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_pai test_pai.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/ai/pai.c")
    add_test(NAME pai COMMAND test_pai)
    xvt_add_known_failure(test_pai plan_text_crlf)
    xvt_add_known_failure(test_pai plan_word_too_long)
    xvt_add_known_failure(test_pai plan_name_unknown)
    xvt_add_known_failure(test_pai plo_table_too_large)
    xvt_add_known_failure(test_pai plo_data_too_large)
endif()

# The mission's goals, triggers, scoring, arrivals and loader: src/xvt/flight/mission/mission.c.
# The loader checks write their mission files into a temporary folder made with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_mission test_mission.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/mission/mission.c")
    add_test(NAME mission COMMAND test_mission)
    xvt_add_known_failure(test_mission bonus_complete_team)
    xvt_add_known_failure(test_mission bonus_failed_team)
    xvt_add_known_failure(test_mission player_hit_not_attacked)
    xvt_add_known_failure(test_mission uncaptured_special_cargo)
    xvt_add_known_failure(test_mission dropoff_group_past_table)
    xvt_add_known_failure(test_mission expired_team_goal_score)
    xvt_add_known_failure(test_mission round_spent_without_mothership)
    xvt_add_known_failure(test_mission missile_boat_second_launcher)
    xvt_add_known_failure(test_mission delivery_resets_rounds)
    xvt_add_known_failure(test_mission attacker_weight_zero)
    xvt_add_known_failure(test_mission tie_third_order_code)
    xvt_add_known_failure(test_mission tie_header_kept)
    xvt_add_known_failure(test_mission tie_goal_text_handles_kept)
endif()

# The frontend's mission loaders and the launch's pilot record setup: src/xvt/frontend/frontend_mission.c.
# The loader checks write their mission list and files into a temporary folder made with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_frontend_mission test_frontend_mission.c
        "${PROJECT_SOURCE_DIR}/src/xvt/frontend/frontend_mission.c")
    add_test(NAME frontend_mission COMMAND test_frontend_mission)
    xvt_add_known_failure(test_frontend_mission briefing_unlisted_mission)
    xvt_add_known_failure(test_frontend_mission briefing_label_past_buffer)
    xvt_add_known_failure(test_frontend_mission faction_past_table)
endif()

# The mission list screens' exit functions: src/xvt/frontend/frontend_mission_list.c.
xvt_add_engine_test(test_frontend_mission_list test_frontend_mission_list.c
    "${PROJECT_SOURCE_DIR}/src/xvt/frontend/frontend_mission_list.c")
add_test(NAME frontend_mission_list COMMAND test_frontend_mission_list)

# The debriefing's standings, outcome text and pages: src/xvt/frontend/mission_debrief.c.
# The outcome text checks write their mission files into a temporary folder made with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_mission_debrief test_mission_debrief.c
        "${PROJECT_SOURCE_DIR}/src/xvt/frontend/mission_debrief.c")
    add_test(NAME mission_debrief COMMAND test_mission_debrief)
    xvt_add_known_failure(test_mission_debrief local_team_rank_by_team_id)
    xvt_add_known_failure(test_mission_debrief left_player_marked_ready)
    xvt_add_known_failure(test_mission_debrief short_file_outcome_text)
    xvt_add_known_failure(test_mission_debrief campaign_id_past_table)
endif()

# The flight's results committed to the pilot record, and the file helpers: src/xvt/flight/fediskio.c.
# The file checks write their files into a temporary folder made with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_fediskio test_fediskio.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/fediskio.c")
    add_test(NAME fediskio COMMAND test_fediskio)
    xvt_add_known_failure(test_fediskio wave_bonus_entry_slot)
    xvt_add_known_failure(test_fediskio teams_8_and_9_results)
    xvt_add_known_failure(test_fediskio ai_level_past_rating_table)
    xvt_add_known_failure(test_fediskio battle_step_past_table)
    xvt_add_known_failure(test_fediskio network_campaign_finished)
endif()

# The goals page's lines: src/xvt/flight/mission/goals.c.
xvt_add_engine_test(test_goals test_goals.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/mission/goals.c")
add_test(NAME goals COMMAND test_goals)
xvt_add_known_failure(test_goals ai_level_escape_text_missing)
xvt_add_known_failure(test_goals condition_47_unloaded_text)
xvt_add_known_failure(test_goals condition_47_past_table)


# The mission setup screens' lists, sequences, rosters, teams and flight assignments:
# src/xvt/frontend/mission_setup.c. The list and sequence checks write their files into a temporary
# folder made with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_mission_setup test_mission_setup.c
        "${PROJECT_SOURCE_DIR}/src/xvt/frontend/mission_setup.c")
    add_test(NAME mission_setup COMMAND test_mission_setup)
    xvt_add_known_failure(test_mission_setup ship_list_over_100)
    xvt_add_known_failure(test_mission_setup ship_list_negative_type)
    xvt_add_known_failure(test_mission_setup team_past_count_table)
    xvt_add_known_failure(test_mission_setup team_gap_hidden)
    xvt_add_known_failure(test_mission_setup auto_assign_no_teams)
    xvt_add_known_failure(test_mission_setup auto_assign_too_few_places)
    xvt_add_known_failure(test_mission_setup departed_assignment_kept)
    xvt_add_known_failure(test_mission_setup first_sequence_count_zero)
    xvt_add_known_failure(test_mission_setup first_sequence_unlisted)
    xvt_add_known_failure(test_mission_setup next_sequence_draw_endless)
    xvt_add_known_failure(test_mission_setup next_sequence_count_zero)
    xvt_add_known_failure(test_mission_setup next_sequence_unlisted)
    xvt_add_known_failure(test_mission_setup rebel_background_unread_mission)
    xvt_add_known_failure(test_mission_setup battle_client_timeout_type)
endif()

# The lobby's DirectPlay layer: its peer slots, sends, receive pump, roster flags and link figures:
# src/xvt/net/net.c. The rename and ready checks run on a frontend display with no window, and an alarm
# set with POSIX calls stops a call that does not return.
if(NOT MSVC)
    xvt_add_engine_test(test_net test_net.c "${PROJECT_SOURCE_DIR}/src/xvt/net/net.c")
    add_test(NAME net COMMAND test_net)
    xvt_add_known_failure(test_net rename_without_session_unlocked)
    xvt_add_known_failure(test_net one_player_send_past_peer_table)
    xvt_add_known_failure(test_net receive_past_peer_table)
endif()

# The flight session's reliable layer: its peer slots, sequence checks and receive queue:
# src/xvt/net/net_reliable.c.
xvt_add_engine_test(test_net_reliable test_net_reliable.c "${PROJECT_SOURCE_DIR}/src/xvt/net/net_reliable.c")
add_test(NAME net_reliable COMMAND test_net_reliable)

# The original flight networking's packet builders and pilot record list: src/xvt/net/flight_net.c.
xvt_add_engine_test(test_flight_net test_flight_net.c "${PROJECT_SOURCE_DIR}/src/xvt/net/flight_net.c")
add_test(NAME flight_net COMMAND test_flight_net)
xvt_add_known_failure(test_flight_net missing_player_marks_first_entry)

# The flight's input histories, remote craft smoothing and world checksums: src/xvt/net/flight_sync.c.
xvt_add_engine_test(test_flight_sync test_flight_sync.c "${PROJECT_SOURCE_DIR}/src/xvt/net/flight_sync.c")
add_test(NAME flight_sync COMMAND test_flight_sync)

# The original flight's network session, its receive order and system messages: src/xvt/net/net_session.c.
xvt_add_engine_test(test_net_session test_net_session.c "${PROJECT_SOURCE_DIR}/src/xvt/net/net_session.c")
add_test(NAME net_session COMMAND test_net_session)
xvt_add_known_failure(test_net_session resent_copy_first_discarded)
xvt_add_known_failure(test_net_session peer_slot_keeps_retry_count)

# The multiplayer frontend's join admission screen and chat panel: src/xvt/net/frontend_net.c; its packet
# handler: src/xvt/net/frontend_net_packets.c and src/xvt/net/frontend_net_setup_packets.c.
# The screen checks write their mission lists and strings into a temporary folder made with POSIX calls, and
# arm an alarm with POSIX calls for a frame that never returns.
if(NOT MSVC)
    xvt_add_engine_test(test_frontend_net test_frontend_net.c
        "${PROJECT_SOURCE_DIR}/src/xvt/net/frontend_net.c"
        "${PROJECT_SOURCE_DIR}/src/xvt/net/frontend_net_packets.c"
        "${PROJECT_SOURCE_DIR}/src/xvt/net/frontend_net_setup_packets.c")
    add_test(NAME frontend_net COMMAND test_frontend_net)
    xvt_add_known_failure(test_frontend_net join_refused_as_full)
    xvt_add_known_failure(test_frontend_net chat_chunk_without_log)
    xvt_add_known_failure(test_frontend_net chat_chunk_past_log)
    xvt_add_known_failure(test_frontend_net craft_option_past_table)
    xvt_add_known_failure(test_frontend_net statistics_past_packet)
endif()

# The briefing map's script player: src/xvt/frontend/briefing_script.c.
xvt_add_engine_test(test_briefing_script test_briefing_script.c
    "${PROJECT_SOURCE_DIR}/src/xvt/frontend/briefing_script.c")
add_test(NAME briefing_script COMMAND test_briefing_script)
xvt_add_known_failure(test_briefing_script end_entry_read_past)
xvt_add_known_failure(test_briefing_script script_without_end_past_words)
xvt_add_known_failure(test_briefing_script opcode_past_count_table)

# The briefing's text buffers: src/xvt/frontend/briefing_text.c. The checks ask the AddressSanitizer
# allocator whether a buffer was freed, which only GCC and Clang provide.
if(NOT MSVC)
    xvt_add_engine_test(test_briefing_text test_briefing_text.c
        "${PROJECT_SOURCE_DIR}/src/xvt/frontend/briefing_text.c")
    add_test(NAME briefing_text COMMAND test_briefing_text)
endif()

# The briefing map's view, flight group pick and drawing: src/xvt/frontend/briefing_map.c. The checks
# arm an alarm with POSIX calls, for a grid that never finishes.
if(NOT MSVC)
    xvt_add_engine_test(test_briefing_map test_briefing_map.c
        "${PROJECT_SOURCE_DIR}/src/xvt/frontend/briefing_map.c")
    add_test(NAME briefing_map COMMAND test_briefing_map)
    xvt_add_known_failure(test_briefing_map grid_zoom_zero)
    xvt_add_known_failure(test_briefing_map zoom_divides_by_zero)
    xvt_add_known_failure(test_briefing_map overlay_craft_type_past_icon_table)
    xvt_add_known_failure(test_briefing_map highlight_craft_type_past_icon_table)
    xvt_add_known_failure(test_briefing_map marker_age_past_32767)
endif()

# The briefing map hooks: src/xvt/frontend/mission_briefing.c.
xvt_add_engine_test(test_mission_briefing test_mission_briefing.c
    "${PROJECT_SOURCE_DIR}/src/xvt/frontend/mission_briefing.c")
add_test(NAME mission_briefing COMMAND test_mission_briefing)

# The craft selection screen's exit and frame: src/xvt/frontend/mission_briefing_craft.c; its ready test and
# the packets the frame answers: src/xvt/frontend/mission_briefing_craft_network.c.
# The frame checks write their mission lists, ship list and strings into a temporary folder made with POSIX
# calls, and arm an alarm with POSIX calls for a frame that never returns.
if(NOT MSVC)
    xvt_add_engine_test(test_mission_briefing_craft test_mission_briefing_craft.c
        "${PROJECT_SOURCE_DIR}/src/xvt/frontend/mission_briefing_craft.c"
        "${PROJECT_SOURCE_DIR}/src/xvt/frontend/mission_briefing_craft_network.c"
        "${PROJECT_SOURCE_DIR}/src/xvt/frontend/mission_briefing.c")
    add_test(NAME mission_briefing_craft COMMAND test_mission_briefing_craft)
    xvt_add_known_failure(test_mission_briefing_craft title_unlisted_mission)
    xvt_add_known_failure(test_mission_briefing_craft ninth_reservation)
    xvt_add_known_failure(test_mission_briefing_craft release_on_full_table)
    xvt_add_known_failure(test_mission_briefing_craft launch_sent_once)
    xvt_add_known_failure(test_mission_briefing_craft ready_flag_unlisted_player)
endif()

# The computer pilots' fighting orders, the wingman's attack and the warhead defense: src/xvt/flight/ai/paifight.c.
xvt_add_engine_test(test_paifight test_paifight.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/ai/paifight.c")
add_test(NAME paifight COMMAND test_paifight)
xvt_add_known_failure(test_paifight follow_leader_attacker_none)
xvt_add_known_failure(test_paifight follow_leader_avoid_target)
xvt_add_known_failure(test_paifight follow_leader_static_skip)
xvt_add_known_failure(test_paifight defense_last_attacker)

# The maneuvers of the computer pilots: src/xvt/flight/ai/paiman.c.
xvt_add_engine_test(test_paiman test_paiman.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/ai/paiman.c")
add_test(NAME paiman COMMAND test_paiman)
xvt_add_known_failure(test_paiman escort_player_craft_push)
xvt_add_known_failure(test_paiman escort_station_past_table)
xvt_add_known_failure(test_paiman speed_away_push_sign)
xvt_add_known_failure(test_paiman board_approach_full_throttle)
xvt_add_known_failure(test_paiman dropoff_no_basis)
xvt_add_known_failure(test_paiman tenth_docking_remembered)
xvt_add_known_failure(test_paiman formation_seventh_place)
xvt_add_known_failure(test_paiman reload_display_other_machine)
xvt_add_known_failure(test_paiman single_waypoint_round)
xvt_add_known_failure(test_paiman destroy_delay_without_countdown)

# The orders of the computer pilots' plans: src/xvt/flight/ai/paiorder.c.
xvt_add_engine_test(test_paiorder test_paiorder.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/ai/paiorder.c")
add_test(NAME paiorder COMMAND test_paiorder)
xvt_add_known_failure(test_paiorder self_destruct_delay_wraps)
xvt_add_known_failure(test_paiorder follower_departure_time)
xvt_add_known_failure(test_paiorder wait_go_other_by_order_code)
xvt_add_known_failure(test_paiorder cannons_abort_reason)
xvt_add_known_failure(test_paiorder hangar_takes_other_followers)
xvt_add_known_failure(test_paiorder hangar_departure_by_hyperspace)

# The collision tests and what a hit does: src/xvt/flight/object/collide.c. The alarm that stops a check whose
# call does not return is POSIX.
if(NOT MSVC)
    xvt_add_engine_test(test_collide test_collide.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/object/collide.c")
    add_test(NAME collide COMMAND test_collide)
    xvt_add_known_failure(test_collide self_reference_hangs)
    xvt_add_known_failure(test_collide reference_pair_hangs)
    xvt_add_known_failure(test_collide static_hit_uncounted)
    xvt_add_known_failure(test_collide impact_keeps_homing)
    xvt_add_known_failure(test_collide explosion_keeps_homing)
endif()

# The static objects' collision test and what a hit on one does: src/xvt/flight/object/static.c.
xvt_add_engine_test(test_static test_static.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/object/static.c")
add_test(NAME static COMMAND test_static)

# The weapons, their shots and the weapon step: src/xvt/flight/object/laser.c.
# The test arms an alarm with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_laser test_laser.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/object/laser.c")
    add_test(NAME laser COMMAND test_laser)
    xvt_add_known_failure(test_laser static_warhead_takes_impact_effect)
    xvt_add_known_failure(test_laser second_flare_same_warhead)
    xvt_add_known_failure(test_laser charged_ion_turbo_fires_torpedo)
    xvt_add_known_failure(test_laser overdrive_message_to_local_player)
endif()

# The object table's slots, copies, fragments and the step that moves every object:
# src/xvt/flight/object/object.c. The test arms an alarm with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_object test_object.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/object/object.c")
    add_test(NAME object COMMAND test_object)
endif()

# The damage page's step through damaged systems: src/xvt/flight/object/damage.c.
xvt_add_engine_test(test_damage test_damage.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/object/damage.c")
add_test(NAME damage COMMAND test_damage)

# The player's throttle steps and disabled-system test: src/xvt/flight/player/flight_player.c.
xvt_add_engine_test(test_flight_player test_flight_player.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/player/flight_player.c")
add_test(NAME flight_player COMMAND test_flight_player)

# The aim cone test and target box sizes: src/xvt/flight/targeting.c.
xvt_add_engine_test(test_targeting test_targeting.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/targeting.c")
add_test(NAME targeting COMMAND test_targeting)
xvt_add_known_failure(test_targeting aim_ahead_past_half_million)
xvt_add_known_failure(test_targeting aim_ahead_past_eight_million)

# How a player step's input is applied, latched and cut by the dead zone: src/xvt/flight/flight_input.c.
xvt_add_engine_test(test_flight_input test_flight_input.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/flight_input.c")
add_test(NAME flight_input COMMAND test_flight_input)

# Handing a player's craft back, the nearest enemy and objective searches: src/xvt/flight/player/player.c.
xvt_add_engine_test(test_player test_player.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/player/player.c")
add_test(NAME player COMMAND test_player)
xvt_add_known_failure(test_player nearest_enemy_none_minus_one)
xvt_add_known_failure(test_player unbind_plan_to_released_craft)
xvt_add_known_failure(test_player objective_second_pair_first_trigger)

# Shield banks, the Tech Library's ratings, warhead kinds and the damage a hit
# does to a craft's components: src/xvt/flight/craft.c.
xvt_add_engine_test(test_craft test_craft.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/craft.c")
add_test(NAME craft COMMAND test_craft)
xvt_add_known_failure(test_craft ion_pulse_has_no_kind)
xvt_add_known_failure(test_craft corvette_bridge_past_95_percent)

# The proving grounds course, its gates and its levels:
# src/xvt/flight/proving_grounds.c. The test arms an alarm with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_proving_grounds test_proving_grounds.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/proving_grounds.c")
    add_test(NAME proving_grounds COMMAND test_proving_grounds)
    xvt_add_known_failure(test_proving_grounds level_21_clock_wraps)
    xvt_add_known_failure(test_proving_grounds level_end_hangs)
    xvt_add_known_failure(test_proving_grounds level_end_restarts_game_clock)
    xvt_add_known_failure(test_proving_grounds level_20_period_past_table)
endif()

# An object's texture animation, the local debris, a player's hyperspace jump
# and the animation step: src/xvt/flight/flight_object.c.
xvt_add_engine_test(test_flight_object test_flight_object.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/flight_object.c")
add_test(NAME flight_object COMMAND test_flight_object)

# The key handler and the timer step: src/xvt/flight/flight.c. The test arms an alarm with POSIX calls.
if(NOT MSVC)
    xvt_add_engine_test(test_flight test_flight.c "${PROJECT_SOURCE_DIR}/src/xvt/flight/flight.c")
    add_test(NAME flight COMMAND test_flight)
    xvt_add_known_failure(test_flight mfd_paging_no_open_page)
    xvt_add_known_failure(test_flight last_team_past_team_table)
    xvt_add_known_failure(test_flight reinforcements_paid_once)
    xvt_add_known_failure(test_flight all_to_shields_empty_lasers)
    xvt_add_known_failure(test_flight all_to_shields_bank_over_limit)
    xvt_add_known_failure(test_flight shields_to_lasers_rear_half)
    xvt_add_known_failure(test_flight shields_to_lasers_full_slot)
    xvt_add_known_failure(test_flight evade_answer_stale_object)
    xvt_add_known_failure(test_flight laser_recharge_damaged_cannons)
    xvt_add_known_failure(test_flight laser_recharge_without_cannons)
    xvt_add_known_failure(test_flight match_speed_settings_above_default)
endif()

# Settings and bindings files: src/xvt_runtime/config/.
# These tests make a temporary folder with POSIX calls and copy the shipped defaults out of the source tree.
if(NOT MSVC)
    xvt_add_engine_test(test_config test_config.c "${PROJECT_SOURCE_DIR}/src/xvt_runtime/config/config.c")
    target_compile_definitions(test_config PRIVATE XVT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME config COMMAND test_config)
    xvt_add_known_failure(test_config import_crlf_line_of_510)

    xvt_add_engine_test(test_controller_config test_controller_config.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/config/controller_config.c")
    target_compile_definitions(test_controller_config PRIVATE XVT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME controller_config COMMAND test_controller_config)
    xvt_add_known_failure(test_controller_config axis_source_unknown_field)

    xvt_add_engine_test(test_keyboard_config test_keyboard_config.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/config/keyboard_config.c")
    target_compile_definitions(test_keyboard_config PRIVATE XVT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME keyboard_config COMMAND test_keyboard_config)

    xvt_add_engine_test(test_mouse_config test_mouse_config.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/config/mouse_config.c")
    target_compile_definitions(test_mouse_config PRIVATE XVT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME mouse_config COMMAND test_mouse_config)

    xvt_add_engine_test(test_settings test_settings.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/config/settings.c")
    target_compile_definitions(test_settings PRIVATE XVT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME settings COMMAND test_settings)

    xvt_add_engine_test(test_video_config test_video_config.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/config/video_config.c")
    target_compile_definitions(test_video_config PRIVATE XVT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME video_config COMMAND test_video_config)
endif()

# Input mapping, capture and flight controls: src/xvt_runtime/input/.
xvt_add_engine_test(test_actions test_actions.c "${PROJECT_SOURCE_DIR}/src/xvt_runtime/input/actions.c")
add_test(NAME actions COMMAND test_actions)
xvt_add_engine_test(test_capture test_capture.c "${PROJECT_SOURCE_DIR}/src/xvt_runtime/input/capture.c")
add_test(NAME capture COMMAND test_capture)
xvt_add_engine_test(test_controller_mapping test_controller_mapping.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/input/controller_mapping.c")
add_test(NAME controller_mapping COMMAND test_controller_mapping)
xvt_add_engine_test(test_controller_options test_controller_options.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/input/controller_options.c")
add_test(NAME controller_options COMMAND test_controller_options)
xvt_add_engine_test(test_flight_controls test_flight_controls.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/input/flight_controls.c")
add_test(NAME flight_controls COMMAND test_flight_controls)
xvt_add_known_failure(test_flight_controls roll_step_fast_rate)
xvt_add_engine_test(test_input_bridge test_input_bridge.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/input/input_bridge.c")
add_test(NAME input_bridge COMMAND test_input_bridge)
xvt_add_engine_test(test_keyboard_mapping test_keyboard_mapping.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/input/keyboard_mapping.c")
add_test(NAME keyboard_mapping COMMAND test_keyboard_mapping)
xvt_add_engine_test(test_mouse_flight test_mouse_flight.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/input/mouse_flight.c")
add_test(NAME mouse_flight COMMAND test_mouse_flight)

# Timing, the orientation hooks, and the remaster's render math.
xvt_add_engine_test(test_flight_integration test_flight_integration.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/timing/flight_integration.c")
add_test(NAME flight_integration COMMAND test_flight_integration)
xvt_add_engine_test(test_flight_timing test_flight_timing.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/timing/flight_timing.c")
add_test(NAME flight_timing COMMAND test_flight_timing)
xvt_add_engine_test(test_host_clock test_host_clock.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/timing/host_clock.c")
add_test(NAME host_clock COMMAND test_host_clock)
xvt_add_engine_test(test_player_timing test_player_timing.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/timing/player_timing.c")
add_test(NAME player_timing COMMAND test_player_timing)
xvt_add_known_failure(test_player_timing slew_int_min)
xvt_add_engine_test(test_reference_motion test_reference_motion.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/timing/reference_motion.c")
add_test(NAME reference_motion COMMAND test_reference_motion)
xvt_add_engine_test(test_orientation_fixed test_orientation_fixed.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/hooks/orientation_fixed.c")
add_test(NAME orientation_fixed COMMAND test_orientation_fixed)
xvt_add_engine_test(test_orientation_hook test_orientation_hook.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/hooks/orientation_hook.c")
add_test(NAME orientation_hook COMMAND test_orientation_hook)
xvt_add_engine_test(test_render_math test_render_math.c
    "${PROJECT_SOURCE_DIR}/src/xvt_remaster/render_math.c")
target_link_libraries(test_render_math PRIVATE xvt_remaster)
add_test(NAME render_math COMMAND test_render_math)

# Storage, the compatibility ports, model assets, launch options and the mission dump tool.
# The tests that read and write files make a fresh temporary folder with POSIX calls (test_temp_folder.h).
if(NOT MSVC)
    xvt_add_engine_test(test_file_io test_file_io.c "${PROJECT_SOURCE_DIR}/src/xvt_runtime/storage/file_io.c")
    add_test(NAME file_io COMMAND test_file_io)
    xvt_add_engine_test(test_storage test_storage.c "${PROJECT_SOURCE_DIR}/src/xvt_runtime/storage/storage.c")
    add_test(NAME storage COMMAND test_storage)
    xvt_add_engine_test(test_frontend_file_list_port test_frontend_file_list_port.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/compat/frontend_file_list_port.c")
    add_test(NAME frontend_file_list_port COMMAND test_frontend_file_list_port)
    xvt_add_engine_test(test_pilot_port test_pilot_port.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/compat/pilot_port.c")
    add_test(NAME pilot_port COMMAND test_pilot_port)
    xvt_add_engine_test(test_opt_native test_opt_native.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/assets/opt_native.c")
    add_test(NAME opt_native COMMAND test_opt_native)
endif()
xvt_add_engine_test(test_opt_relocate test_opt_relocate.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/assets/opt_relocate.c")
add_test(NAME opt_relocate COMMAND test_opt_relocate)
xvt_add_engine_test(test_host_config test_host_config.c "${PROJECT_SOURCE_DIR}/src/xvt_app/host_config.c")
add_test(NAME host_config COMMAND test_host_config)
# The run log file: names, which old files are kept, and the append-only handle under two writing threads.
if(NOT MSVC)
    find_package(Threads REQUIRED)
    xvt_add_test_program(test_log_file test_log_file.c "${PROJECT_SOURCE_DIR}/src/xvt_app/log_file.c")
    target_include_directories(test_log_file PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(test_log_file PRIVATE Threads::Threads)
    add_test(NAME log_file COMMAND test_log_file)
endif()
# The filter on libjuice's own log: its fixed reasons for a failing link pass, a message carrying a
# credential, SDP or an address never does.
xvt_add_test_program(test_dplay_ice_log test_dplay_ice_log.c)
target_include_directories(test_dplay_ice_log PRIVATE "${PROJECT_SOURCE_DIR}/aeron/src")
add_test(NAME dplay_ice_log COMMAND test_dplay_ice_log)
# The crash note crashes child processes on purpose, so its test is the one program built without the
# sanitizers: AddressSanitizer would catch each crash itself and end the child its own way.
add_executable(test_crash_note test_crash_note.c "${PROJECT_SOURCE_DIR}/src/xvt_app/crash_note.c"
    "${PROJECT_SOURCE_DIR}/src/xvt_app/log_file.c")
target_include_directories(test_crash_note PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
if(NOT MSVC)
    target_compile_options(test_crash_note PRIVATE -Wall -Wextra -Wpedantic -fno-omit-frame-pointer -g)
endif()
add_dependencies(xvt_tests test_crash_note)
add_test(NAME crash_note COMMAND test_crash_note)
# The mission dump tool is built here as the top-level build builds it with XVT_BUILD_TOOLS, but with the
# test flags, and the test runs it on mission files it writes itself.
if(NOT MSVC)
    xvt_add_test_program(mission_dump_under_test "${PROJECT_SOURCE_DIR}/tools/mission_dump.c")
    target_include_directories(mission_dump_under_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/aeron/include")
    xvt_add_test_program(test_mission_dump test_mission_dump.c)
    target_include_directories(test_mission_dump PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/aeron/include")
    target_compile_definitions(test_mission_dump PRIVATE
        "MISSION_DUMP_TOOL=\"$<TARGET_FILE:mission_dump_under_test>\"")
    add_test(NAME mission_dump COMMAND test_mission_dump)
endif()

# The cockpit's snapshot capture: src/xvt_runtime/snapshot/cockpit_*.
xvt_add_engine_test(test_cockpit_capture test_cockpit_capture.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/cockpit_capture.c")
add_test(NAME cockpit_capture COMMAND test_cockpit_capture)
xvt_add_known_failure(test_cockpit_capture export_resources_without_panel)
xvt_add_engine_test(test_cockpit_instruments test_cockpit_instruments.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/cockpit_instruments.c")
add_test(NAME cockpit_instruments COMMAND test_cockpit_instruments)
xvt_add_engine_test(test_cockpit_messages test_cockpit_messages.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/cockpit_messages.c")
add_test(NAME cockpit_messages COMMAND test_cockpit_messages)
xvt_add_engine_test(test_cockpit_pages test_cockpit_pages.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/cockpit_pages.c")
add_test(NAME cockpit_pages COMMAND test_cockpit_pages)
xvt_add_engine_test(test_cockpit_readouts test_cockpit_readouts.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/cockpit_readouts.c")
add_test(NAME cockpit_readouts COMMAND test_cockpit_readouts)
xvt_add_engine_test(test_cockpit_text test_cockpit_text.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/cockpit_text.c")
add_test(NAME cockpit_text COMMAND test_cockpit_text)

# Snapshot records and the render snapshot: src/xvt_runtime/snapshot/.
xvt_add_engine_test(test_records test_records.c "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/records.c")
add_test(NAME records COMMAND test_records)
# The asset registry, and the cockpit bindings that register through it, resolve paths through storage: their
# tests write the files they register into a temporary folder made with POSIX calls (test_temp_folder.h).
if(NOT MSVC)
    xvt_add_engine_test(test_render_assets test_render_assets.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/render_assets.c")
    add_test(NAME render_assets COMMAND test_render_assets)
endif()
xvt_add_engine_test(test_render_camera test_render_camera.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/render_camera.c")
add_test(NAME render_camera COMMAND test_render_camera)
xvt_add_engine_test(test_render_capture test_render_capture.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/render_capture.c")
add_test(NAME render_capture COMMAND test_render_capture)
if(NOT MSVC)
    xvt_add_engine_test(test_render_cockpit_assets test_render_cockpit_assets.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/render_cockpit_assets.c")
    add_test(NAME render_cockpit_assets COMMAND test_render_cockpit_assets)
endif()
xvt_add_engine_test(test_render_frontend test_render_frontend.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/render_frontend.c")
add_test(NAME render_frontend COMMAND test_render_frontend)
xvt_add_engine_test(test_render_hud test_render_hud.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/render_hud.c")
add_test(NAME render_hud COMMAND test_render_hud)
xvt_add_engine_test(test_render_map test_render_map.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/render_map.c")
add_test(NAME render_map COMMAND test_render_map)
xvt_add_engine_test(test_render_snapshot test_render_snapshot.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/render_snapshot.c")
add_test(NAME render_snapshot COMMAND test_render_snapshot)

# The flight task, its network frame and resync: src/xvt_runtime/runtime/.
xvt_add_engine_test(test_flight_checkpoint test_flight_checkpoint.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_checkpoint.c")
add_test(NAME flight_checkpoint COMMAND test_flight_checkpoint)
# The entry test makes a temporary folder with POSIX calls and copies the shipped defaults into it.
if(NOT MSVC)
    xvt_add_engine_test(test_flight_entry test_flight_entry.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_entry.c")
    target_compile_definitions(test_flight_entry PRIVATE XVT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME flight_entry COMMAND test_flight_entry)
endif()
xvt_add_engine_test(test_flight_frame test_flight_frame.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_frame.c")
add_test(NAME flight_frame COMMAND test_flight_frame)
xvt_add_engine_test(test_flight_record test_flight_record.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_record.c")
add_test(NAME flight_record COMMAND test_flight_record)
xvt_add_engine_test(test_flight_loading test_flight_loading.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_loading.c")
add_test(NAME flight_loading COMMAND test_flight_loading)
xvt_add_engine_test(test_flight_messages test_flight_messages.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_messages.c")
add_test(NAME flight_messages COMMAND test_flight_messages)
xvt_add_engine_test(test_flight_network test_flight_network.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_network.c"
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_network_exchange.c")
add_test(NAME flight_network COMMAND test_flight_network)
xvt_add_engine_test(test_flight_network_exchange test_flight_network_exchange.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_network_exchange.c"
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_network.c")
add_test(NAME flight_network_exchange COMMAND test_flight_network_exchange)
xvt_add_engine_test(test_flight_packets test_flight_packets.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_packets.c")
add_test(NAME flight_packets COMMAND test_flight_packets)
xvt_add_engine_test(test_flight_prediction test_flight_prediction.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_prediction.c")
add_test(NAME flight_prediction COMMAND test_flight_prediction)
xvt_add_engine_test(test_flight_sim test_flight_sim.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_sim.c")
add_test(NAME flight_sim COMMAND test_flight_sim)
xvt_add_engine_test(test_flight_task test_flight_task.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_task.c")
add_test(NAME flight_task COMMAND test_flight_task)
xvt_add_engine_test(test_flight_wire test_flight_wire.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_wire.c")
add_test(NAME flight_wire COMMAND test_flight_wire)
xvt_add_engine_test(test_resync_task test_resync_task.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/resync_task.c")
add_test(NAME resync_task COMMAND test_resync_task)

# The other runtime tasks: frontend, campaign, movies, dialogs and the network session.
# The tests that run frontend frames give the frontend a display with no window (test_frontend_display.h); the
# ones that look up game files by name make a temporary asset folder with POSIX calls (test_asset_folder.h).
if(NOT MSVC)
    xvt_add_engine_test(test_campaign_task test_campaign_task.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/campaign_task.c")
    add_test(NAME campaign_task COMMAND test_campaign_task)
endif()
xvt_add_engine_test(test_cd_task test_cd_task.c "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/cd_task.c")
add_test(NAME cd_task COMMAND test_cd_task)
if(NOT MSVC)
    xvt_add_engine_test(test_cutscene_task test_cutscene_task.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/cutscene_task.c")
    add_test(NAME cutscene_task COMMAND test_cutscene_task)
endif()
xvt_add_engine_test(test_dialog_task test_dialog_task.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/dialog_task.c")
add_test(NAME dialog_task COMMAND test_dialog_task)
xvt_add_engine_test(test_frontend_actions test_frontend_actions.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/frontend_actions.c")
add_test(NAME frontend_actions COMMAND test_frontend_actions)
xvt_add_engine_test(test_frontend_cleanup test_frontend_cleanup.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/frontend_cleanup.c")
add_test(NAME frontend_cleanup COMMAND test_frontend_cleanup)
if(NOT MSVC)
    xvt_add_engine_test(test_frontend_movies test_frontend_movies.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/frontend_movies.c")
    add_test(NAME frontend_movies COMMAND test_frontend_movies)
endif()
xvt_add_engine_test(test_frontend_task test_frontend_task.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/frontend_task.c")
add_test(NAME frontend_task COMMAND test_frontend_task)
xvt_add_engine_test(test_launch_task test_launch_task.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/launch_task.c")
add_test(NAME launch_task COMMAND test_launch_task)
xvt_add_engine_test(test_mission_dialogs test_mission_dialogs.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/mission_dialogs.c")
add_test(NAME mission_dialogs COMMAND test_mission_dialogs)
xvt_add_engine_test(test_movie_sync test_movie_sync.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/movie_sync.c")
add_test(NAME movie_sync COMMAND test_movie_sync)
if(NOT MSVC)
    xvt_add_engine_test(test_movie_task test_movie_task.c
        "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/movie_task.c")
    add_test(NAME movie_task COMMAND test_movie_task)
endif()
xvt_add_engine_test(test_network_browser test_network_browser.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/network_browser.c")
add_test(NAME network_browser COMMAND test_network_browser)
xvt_add_engine_test(test_network_dialogs test_network_dialogs.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/network_dialogs.c")
add_test(NAME network_dialogs COMMAND test_network_dialogs)
xvt_add_engine_test(test_network_metadata test_network_metadata.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/network_metadata.c")
add_test(NAME network_metadata COMMAND test_network_metadata)
xvt_add_engine_test(test_network_session test_network_session.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/network_session.c")
add_test(NAME network_session COMMAND test_network_session)
xvt_add_engine_test(test_network_task test_network_task.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/network_task.c")
add_test(NAME network_task COMMAND test_network_task)
xvt_add_engine_test(test_port test_port.c "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/port.c")
add_test(NAME port COMMAND test_port)
xvt_add_engine_test(test_presentation test_presentation.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/presentation.c")
add_test(NAME presentation COMMAND test_presentation)

xvt_add_test_program(harness_catches_failure harness_catches_failure.c)
add_test(NAME harness_catches_failure COMMAND harness_catches_failure)
set_tests_properties(harness_catches_failure PROPERTIES WILL_FAIL TRUE)

if(NOT MSVC)
    xvt_add_test_program(harness_catches_asan harness_catches_asan.c)
    add_test(NAME harness_catches_asan COMMAND harness_catches_asan)
    set_tests_properties(harness_catches_asan PROPERTIES
        PASS_REGULAR_EXPRESSION "AddressSanitizer: heap-buffer-overflow")

    xvt_add_test_program(harness_catches_ubsan harness_catches_ubsan.c)
    add_test(NAME harness_catches_ubsan COMMAND harness_catches_ubsan)
    set_tests_properties(harness_catches_ubsan PROPERTIES
        PASS_REGULAR_EXPRESSION "runtime error: signed integer overflow")
endif()

# The log catalog (src/xvt_runtime/log/events.json) and the XVT_LOG_* call sites must agree; the
# check is a Python 3 script, so it is a test only where an interpreter is found.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME log_catalog
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/log_catalog_check.py"
                "${PROJECT_SOURCE_DIR}")
else()
    message(STATUS "Python 3 was not found; the log catalog check is not added as a test")
endif()

# UBSan prints and carries on by default; a test must stop at the first report.
# The timeout turns a hung test into a failure instead of a stalled run.
get_property(XVT_TEST_NAMES DIRECTORY PROPERTY TESTS)
set_tests_properties(${XVT_TEST_NAMES} PROPERTIES
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
    TIMEOUT 60)

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
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/snapshot/world_state.c")
add_test(NAME world_state COMMAND test_world_state)

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

# The flight task, its network frame and resync: src/xvt_runtime/runtime/.
xvt_add_engine_test(test_flight_messages test_flight_messages.c
    "${PROJECT_SOURCE_DIR}/src/xvt_runtime/runtime/flight_messages.c")
add_test(NAME flight_messages COMMAND test_flight_messages)

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

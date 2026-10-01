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

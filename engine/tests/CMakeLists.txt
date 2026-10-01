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

# UBSan prints and carries on by default; a test must stop at the first report.
# The timeout turns a hung test into a failure instead of a stalled run.
get_property(XVT_TEST_NAMES DIRECTORY PROPERTY TESTS)
set_tests_properties(${XVT_TEST_NAMES} PROPERTIES
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
    TIMEOUT 60)

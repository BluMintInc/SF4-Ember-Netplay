# Build the exact bridge used by this CMake tree. Missing tools and failed
# integration prerequisites fail this required gate instead of skipping tests.
function(require_success)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
        WORKING_DIRECTORY "${SOURCE}/sdk/typescript")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "SDK validation failed (${result}): ${ARGV}")
    endif()
endfunction()
set(BRIDGE "${EMBER_TARGET}/debug/ember-bridge.exe")
# On the processors CTest reserves for this test (SF4E_CARGO_TEST_JOBS).
if(NOT JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "SDK validation needs JOBS, a positive integer")
endif()
require_success("${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${EMBER_TARGET}"
    "${CARGO}" build --locked -j ${JOBS} --manifest-path "${SOURCE}/server/ember/Cargo.toml" -p ember-bridge)
if(NOT EXISTS "${BRIDGE}")
    message(FATAL_ERROR "SDK bridge prerequisite is missing: ${BRIDGE}")
endif()
require_success("${NPM}" ci --ignore-scripts)
require_success("${NPM}" run build)
require_success("${CMAKE_COMMAND}" -E env "EMBER_BRIDGE_TEST_BINARY=${BRIDGE}" "${NPM}" test)

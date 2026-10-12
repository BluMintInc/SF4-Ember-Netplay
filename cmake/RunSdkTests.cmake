# Build the exact bridge used by this CMake tree. Missing tools and failed
# integration prerequisites fail this required gate instead of skipping tests.
function(require_success)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
        WORKING_DIRECTORY "${SOURCE}/sdk/typescript")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "SDK validation failed (${result}): ${ARGV}")
    endif()
endfunction()
require_success("${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${BUILD}/rust-ember-target"
    "${CARGO}" build --locked --manifest-path "${SOURCE}/server/ember/Cargo.toml" -p ember-bridge)
if(NOT EXISTS "${BRIDGE}")
    message(FATAL_ERROR "SDK bridge prerequisite is missing: ${BRIDGE}")
endif()
require_success("${NPM}" ci --ignore-scripts)
require_success("${NPM}" run build)
require_success("${CMAKE_COMMAND}" -E env "EMBER_BRIDGE_TEST_BINARY=${BRIDGE}" "${NPM}" test)

# Tests of Frog as the appliance drives it: the headless plugin behind
# rflh::IPlug2HeadlessProcessor. The engine's own tests stay in
# Frog/engine/tests (run-tests.sh); these need the whole plugin class.
# platform/linux/docker-build-arm64.sh builds and runs them.

add_executable(shell_test shell_test.cpp)
target_link_libraries(shell_test PRIVATE rflh_host frog_plugin)
target_compile_options(shell_test PRIVATE -Wall -Wno-unknown-pragmas -Wno-multichar -Wno-comment)
set_target_properties(shell_test PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

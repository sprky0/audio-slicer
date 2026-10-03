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

# F10: the editor offscreen through the module; writes the panel at
# 1024 x 600 (edit, playing, perform, selected) and 1280 x 720 to DIR
if(FROG_APPLIANCE_EDITOR)
	add_executable(editor_shot_test editor_shot_test.cpp)
	target_link_libraries(editor_shot_test PRIVATE rflh_host frog_plugin_ui)
	add_dependencies(editor_shot_test frog_ui_resources frog_editor)
	set_target_properties(editor_shot_test PROPERTIES ENABLE_EXPORTS ON)
	target_compile_options(editor_shot_test PRIVATE -Wall -Wno-unknown-pragmas -Wno-multichar -Wno-comment)
	set_target_properties(editor_shot_test PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	)
endif()

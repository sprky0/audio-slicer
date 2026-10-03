# frog-appliance: the Linux appliance binary. Links the shared host library
# and Frog's plugin class (ratfactory-linux-host docs/PORTING_A_PLUGIN.md §2a):
# with its editor for the panel (frog_plugin_ui plus the libfrog-editor.so
# module, the default), or editor-less (frog_plugin, -DFROG_APPLIANCE_EDITOR=OFF).

add_executable(frog-appliance main.cpp)
if(FROG_APPLIANCE_EDITOR)
	target_link_libraries(frog-appliance PRIVATE rflh_host frog_plugin_ui)
	add_dependencies(frog-appliance frog_ui_resources frog_editor)
	# No GPU library in the binary: the editor module is loaded after the
	# first audio callback and takes IGraphics from the binary, which exports
	# its symbols for it (-rdynamic).
	set_target_properties(frog-appliance PROPERTIES ENABLE_EXPORTS ON)
else()
	target_link_libraries(frog-appliance PRIVATE rflh_host frog_plugin)
endif()
target_compile_options(frog-appliance PRIVATE -Wall -Wno-unknown-pragmas -Wno-multichar -Wno-comment)

set_target_properties(frog-appliance PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

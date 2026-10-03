# The engine's source list, shared by Frog/CMakeLists.txt (desktop formats),
# platform/linux (the appliance) and the tools. Include it, then use
# FROG_ENGINE_SOURCES / FROG_ENGINE_INCLUDE. C11; no iPlug2 dependency.

set(FROG_ENGINE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(FROG_ENGINE_INCLUDE "${FROG_ENGINE_DIR}")
file(GLOB FROG_ENGINE_SOURCES CONFIGURE_DEPENDS "${FROG_ENGINE_DIR}/*.c")
list(APPEND FROG_ENGINE_SOURCES "${FROG_ENGINE_DIR}/third_party/cJSON.c")

# frog_engine: the static library every consumer links.
function(frog_add_engine_library)
	if(TARGET frog_engine)
		return()
	endif()
	enable_language(C)
	add_library(frog_engine STATIC ${FROG_ENGINE_SOURCES})
	target_include_directories(frog_engine PUBLIC "${FROG_ENGINE_INCLUDE}")
	set_target_properties(frog_engine PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
	target_compile_options(frog_engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
	set_source_files_properties("${FROG_ENGINE_DIR}/third_party/cJSON.c" PROPERTIES COMPILE_OPTIONS "-w")
	if(UNIX AND NOT APPLE)
		target_compile_definitions(frog_engine PRIVATE _POSIX_C_SOURCE=200809L)
	endif()
	find_library(FROG_LIBM m)
	if(FROG_LIBM)
		target_link_libraries(frog_engine PUBLIC ${FROG_LIBM})
	endif()
endfunction()

# frog-render: session JSON + samples → WAV, the parity and CPU-budget tool.
function(frog_add_render_tool)
	if(TARGET frog-render)
		return()
	endif()
	frog_add_engine_library()
	add_executable(frog-render "${FROG_ENGINE_DIR}/../tools/render/main.c")
	target_link_libraries(frog-render PRIVATE frog_engine)
	set_target_properties(frog-render PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
endfunction()

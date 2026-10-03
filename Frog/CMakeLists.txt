cmake_minimum_required(VERSION 3.14)
project(Frog)

if(NOT DEFINED IPLUG2_DIR)
	set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

# Build stamp: regenerate version.h before every build. ALL target with no
# BYPRODUCTS so it always runs; the script no-ops the write when only the
# timestamp changed, so this does not force a rebuild.
add_custom_target(FrogVersionStamp ALL
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/stamp-version.sh"
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	COMMENT "Generating Frog/version.h"
	VERBATIM
)

# The C11 engine: a static library every plugin format links, plus the
# frog-render tool (session JSON + samples → WAV).
include(engine/engine.cmake)
frog_add_engine_library()
frog_add_render_tool()

iplug_add_plugin(${PROJECT_NAME}
	SOURCES
		Frog.cpp
		Frog.h
		ui/FrogVersion.h
		resources/resource.h
	RESOURCES
		resources/fonts/Roboto-Regular.ttf
)

# iplug_add_plugin creates one target per format (Frog-app, Frog-vst3, ...).
foreach(fmt app vst3 au clap aax)
	if(TARGET ${PROJECT_NAME}-${fmt})
		add_dependencies(${PROJECT_NAME}-${fmt} FrogVersionStamp)
		target_link_libraries(${PROJECT_NAME}-${fmt} PRIVATE frog_engine)
	endif()
endforeach()

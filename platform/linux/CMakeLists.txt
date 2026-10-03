cmake_minimum_required(VERSION 3.16)

# Frog Linux appliance (ROADMAP.md F3; ratfactory-linux-host LINUX_ROADMAP.md
# L5 / L15): the binary `frog-appliance` links the shared host library
# `libratfactory-linux-host` and Frog's plugin class. This is the project
# root for the Linux build only; the macOS / Windows plugin formats use
# Frog/CMakeLists.txt.
#
#   cmake -S platform/linux -B build-linux \
#         -DIPLUG2_DIR=/path/to/iPlug2 (the Rat Factory fork) \
#         -DRFLH_ROOT=/path/to/ratfactory-linux-host
#   cmake --build build-linux -j
#
# platform/linux/docker-build-arm64.sh wraps this for a linux/arm64 cross-build.

project(frog-appliance LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(FROG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
get_filename_component(FROG_ROOT "${FROG_ROOT}" ABSOLUTE)

# The iPlug2 checkout: the Rat Factory fork (submodule iPlug2, branch
# ratfactory-linux). The Headless API and IGraphicsKMS live only there.
set(IPLUG2_DIR "${FROG_ROOT}/iPlug2" CACHE PATH "iPlug2 checkout (Rat Factory fork)")
if(NOT EXISTS "${IPLUG2_DIR}/IPlug/Headless/IPlugHeadless.h")
	message(FATAL_ERROR
		"IPLUG2_DIR=${IPLUG2_DIR} has no IPlug/Headless/IPlugHeadless.h. "
		"Point it at the Rat Factory iPlug2 fork (branch ratfactory-linux): git submodule update --init iPlug2")
endif()

# Peer-repo checkout of the shared host (its docs/PORTING_A_PLUGIN.md §2a).
set(RFLH_ROOT "${FROG_ROOT}/../ratfactory-linux-host" CACHE PATH "Path to a ratfactory-linux-host checkout")
get_filename_component(RFLH_ROOT "${RFLH_ROOT}" ABSOLUTE)
if(NOT EXISTS "${RFLH_ROOT}/host/CMakeLists.txt")
	message(FATAL_ERROR "RFLH_ROOT=${RFLH_ROOT} is not a ratfactory-linux-host checkout")
endif()
# EXCLUDE_FROM_ALL: the host's own example binary (rflh-poc) is not part of
# this build; the rflh_host library target is what the appliance links.
add_subdirectory("${RFLH_ROOT}" ratfactory-linux-host-build EXCLUDE_FROM_ALL)

# The appliance with Frog's editor on the panel (DRM/KMS, EGL, GLES2): the
# binary links no GPU library; the editor's GPU side is lib/libfrog-editor.so,
# loaded after the first audio callback. OFF builds the editor-less appliance.
option(FROG_APPLIANCE_EDITOR "frog-appliance with its editor on the panel (IGraphicsKMS)" ON)

add_subdirectory(frog-plugin)
add_subdirectory(frog-appliance)

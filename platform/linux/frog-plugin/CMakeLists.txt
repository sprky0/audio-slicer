# frog_plugin: Frog's plugin class and the slice of iPlug2 it needs, compiled
# editor-less on top of the Headless API. frog-appliance links it (or
# frog_plugin_ui below); the engine tests never need iPlug2 at all.

set(FROG_DIR "${FROG_ROOT}/Frog")

# version.h from config.h + git; the script no-ops when nothing changed.
add_custom_target(frog_generated
	COMMAND "${FROG_DIR}/scripts/stamp-version.sh"
	WORKING_DIRECTORY "${FROG_DIR}"
	COMMENT "Generating Frog/version.h"
	VERBATIM
)

set(IPLUG2_SOURCES
	${IPLUG2_DIR}/IPlug/IPlugAPIBase.cpp
	${IPLUG2_DIR}/IPlug/IPlugPluginBase.cpp
	${IPLUG2_DIR}/IPlug/IPlugProcessor.cpp
	${IPLUG2_DIR}/IPlug/IPlugParameter.cpp
	${IPLUG2_DIR}/IPlug/IPlugTimer.cpp
	${IPLUG2_DIR}/IPlug/IPlugPaths.cpp
	${IPLUG2_DIR}/IPlug/Headless/IPlugHeadless.cpp
)

# The engine (C11) joins this list from F5 on: ${FROG_DIR}/engine/*.c.
set(FROG_SOURCES
	${FROG_DIR}/Frog.cpp
)

set(FROG_INCLUDES
	${FROG_DIR}
	${IPLUG2_DIR}/IPlug
	${IPLUG2_DIR}/IPlug/Headless
	${IPLUG2_DIR}/IPlug/Extras
	${IPLUG2_DIR}/WDL
	${IPLUG2_DIR}/Dependencies/Extras
)

add_library(frog_plugin STATIC
	${FROG_SOURCES}
	${IPLUG2_SOURCES}
)
add_dependencies(frog_plugin frog_generated)

target_include_directories(frog_plugin
	PUBLIC
		${FROG_INCLUDES}
	PRIVATE
		${IPLUG2_DIR}/IGraphics
		${IPLUG2_DIR}/IGraphics/Controls
		${IPLUG2_DIR}/IGraphics/Drawing
		${IPLUG2_DIR}/IGraphics/Extras
)

# FROG_APPLIANCE: built for the Linux appliance.
target_compile_definitions(frog_plugin PUBLIC
	FROG_APPLIANCE
	HEADLESS_API
	IPLUG_DSP=1
	IPLUG_EDITOR=0
	NO_IGRAPHICS
	SAMPLE_TYPE_DOUBLE
)

# ARM Linux `char` is unsigned by default and WDL refuses to build that way;
# keep the ABI the code was written against.
target_compile_options(frog_plugin PUBLIC -fsigned-char)
target_compile_options(frog_plugin PRIVATE -Wall -Wno-unused -Wno-sign-compare -Wno-reorder -Wno-unknown-pragmas -Wno-multichar -Wno-comment)
set_source_files_properties(${IPLUG2_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

find_package(Threads REQUIRED)
target_link_libraries(frog_plugin PUBLIC Threads::Threads)

# frog_plugin_ui: the same plugin class with its editor compiled in, for the
# panel: IGraphics with NanoVG on GLES2, drawn on DRM/KMS through EGL + GBM by
# the fork's IGraphicsKMS, touch from evdev. Everything that touches EGL,
# GLES2, GBM or libdrm (IGraphicsKMS, IGraphicsNanoVG, NanoVG) lives in
# frog_editor, a shared object (lib/libfrog-editor.so) the appliance loads
# after its first audio callback; the binary links no GPU library.
if(FROG_APPLIANCE_EDITOR)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(FROG_KMS REQUIRED IMPORTED_TARGET libdrm gbm egl glesv2)
	enable_language(C)

	# The binary and the module must come from the same build (the C++ ABI
	# between them is IGraphics' class layout): both carry this id, and the
	# appliance refuses a module whose id differs.
	execute_process(COMMAND git -c safe.directory=* -C "${IPLUG2_DIR}" rev-parse --short=12 HEAD
		OUTPUT_VARIABLE FROG_IPLUG2_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	if(NOT FROG_IPLUG2_SHA AND EXISTS "${IPLUG2_DIR}/.git")
		# no git in the build image: a submodule's .git names its git dir,
		# whose HEAD is the commit (detached, as a pinned submodule is)
		set(_head "")
		if(IS_DIRECTORY "${IPLUG2_DIR}/.git")
			set(_gitdir "${IPLUG2_DIR}/.git")
		else()
			file(STRINGS "${IPLUG2_DIR}/.git" _gitline LIMIT_COUNT 1)
			string(REGEX REPLACE "^gitdir: *" "" _gitdir "${_gitline}")
			get_filename_component(_gitdir "${_gitdir}" ABSOLUTE BASE_DIR "${IPLUG2_DIR}")
		endif()
		if(EXISTS "${_gitdir}/HEAD")
			file(STRINGS "${_gitdir}/HEAD" _head LIMIT_COUNT 1)
		endif()
		if(_head MATCHES "^ref: *(.+)$" AND EXISTS "${_gitdir}/${CMAKE_MATCH_1}")
			file(STRINGS "${_gitdir}/${CMAKE_MATCH_1}" _head LIMIT_COUNT 1)
		endif()
		if(_head MATCHES "^[0-9a-f]+$")
			string(SUBSTRING "${_head}" 0 12 FROG_IPLUG2_SHA)
		endif()
	endif()
	if(NOT FROG_IPLUG2_SHA)
		set(FROG_IPLUG2_SHA "unknown")
	endif()
	string(TIMESTAMP FROG_EDITOR_STAMP "%Y%m%dT%H%M%SZ" UTC)
	set(FROG_EDITOR_BUILD_ID "frog iPlug2 ${FROG_IPLUG2_SHA} configured ${FROG_EDITOR_STAMP}")

	set(IGRAPHICS_SOURCES
		${IPLUG2_DIR}/IGraphics/IGraphics.cpp
		${IPLUG2_DIR}/IGraphics/IGraphicsEditorDelegate.cpp
		${IPLUG2_DIR}/IGraphics/IControl.cpp
		${IPLUG2_DIR}/IGraphics/Controls/IControls.cpp
		${IPLUG2_DIR}/IGraphics/Controls/IPopupMenuControl.cpp
		${IPLUG2_DIR}/IGraphics/Controls/ITextEntryControl.cpp
	)
	# the editor module: the GPU side
	set(IGRAPHICS_KMS_SOURCES
		${IPLUG2_DIR}/IGraphics/Drawing/IGraphicsNanoVG.cpp
		${IPLUG2_DIR}/IGraphics/Platforms/IGraphicsKMS.cpp
		${IPLUG2_DIR}/Dependencies/IGraphics/NanoVG/src/nanovg.c
	)
	set(FROG_UI_INCLUDES
		${FROG_INCLUDES}
		${IPLUG2_DIR}/IGraphics
		${IPLUG2_DIR}/IGraphics/Controls
		${IPLUG2_DIR}/IGraphics/Drawing
		${IPLUG2_DIR}/IGraphics/Platforms
		${IPLUG2_DIR}/IGraphics/Extras
		${IPLUG2_DIR}/Dependencies/IGraphics/NanoVG/src
		${IPLUG2_DIR}/Dependencies/IGraphics/NanoSVG/src
		${IPLUG2_DIR}/Dependencies/IGraphics/STB
		${RFLH_ROOT}/host
		${RFLH_ROOT}/host/iplug2
		${FROG_KMS_INCLUDE_DIRS}
	)
	set(FROG_UI_DEFINES
		FROG_APPLIANCE
		HEADLESS_API
		IPLUG_DSP=1
		IPLUG_EDITOR=1
		IGRAPHICS_NANOVG
		IGRAPHICS_GLES2
		IGRAPHICS_KMS
		SAMPLE_TYPE_DOUBLE
		"RFLH_EDITOR_BUILD_ID=\"${FROG_EDITOR_BUILD_ID}\""
	)

	add_library(frog_plugin_ui STATIC
		${FROG_SOURCES}
		${IPLUG2_SOURCES}
		${IGRAPHICS_SOURCES}
	)
	add_dependencies(frog_plugin_ui frog_generated)
	target_include_directories(frog_plugin_ui PUBLIC ${FROG_UI_INCLUDES})
	target_compile_definitions(frog_plugin_ui PUBLIC ${FROG_UI_DEFINES} IGRAPHICS_KMS_MODULE)
	target_compile_options(frog_plugin_ui PUBLIC -fsigned-char)
	target_compile_options(frog_plugin_ui PRIVATE
		$<$<COMPILE_LANGUAGE:CXX>:-Wall -Wno-unused -Wno-sign-compare -Wno-reorder -Wno-unknown-pragmas -Wno-multichar -Wno-comment>)
	set_source_files_properties(${IGRAPHICS_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
	# no GPU library: the module brings them
	target_link_libraries(frog_plugin_ui PUBLIC Threads::Threads m)

	# libfrog-editor.so: IGraphicsKMS, IGraphicsNanoVG, NanoVG and the module's
	# entry point; the rest of IGraphics comes from the binary that loads it.
	# DT_RPATH $ORIGIN (not RUNPATH): Mesa is installed beside it.
	add_library(frog_editor SHARED
		${IGRAPHICS_KMS_SOURCES}
		${RFLH_ROOT}/host/iplug2/IGraphicsKMSEditorModule.cpp
	)
	add_dependencies(frog_editor frog_generated)
	target_include_directories(frog_editor PRIVATE ${FROG_UI_INCLUDES})
	target_compile_definitions(frog_editor PRIVATE ${FROG_UI_DEFINES})
	target_compile_options(frog_editor PRIVATE -fsigned-char)
	set_source_files_properties(${IGRAPHICS_KMS_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
	target_link_libraries(frog_editor PRIVATE PkgConfig::FROG_KMS Threads::Threads m ${CMAKE_DL_LIBS})
	set_target_properties(frog_editor PROPERTIES
		OUTPUT_NAME frog-editor
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
		BUILD_RPATH "\$ORIGIN"
		SKIP_BUILD_RPATH OFF
	)
	target_link_options(frog_editor PRIVATE "LINKER:--disable-new-dtags")

	# the editor's fonts and SVGs beside the binary, <exe dir>/resources/{fonts,img}
	# (IPlugPaths' LocateResource on Linux; /opt/ratfactory/bin/resources on the Pi)
	add_custom_target(frog_ui_resources ALL
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/resources/fonts" "${CMAKE_BINARY_DIR}/bin/resources/img"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FROG_DIR}/resources/fonts/Roboto-Regular.ttf "${CMAKE_BINARY_DIR}/bin/resources/fonts/"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FROG_DIR}/resources/img/frog-wordmark.svg ${FROG_DIR}/resources/img/rat-factory.svg "${CMAKE_BINARY_DIR}/bin/resources/img/"
		VERBATIM
	)
endif()

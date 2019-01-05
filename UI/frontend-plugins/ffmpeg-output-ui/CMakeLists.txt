project(ffmpeg-output-ui)

find_package(FFmpeg REQUIRED COMPONENTS avcodec avdevice avutil avformat)

include_directories(${FFMPEG_INCLUDE_DIRS})
include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/deps/libff")
if(APPLE)
	find_library(COCOA Cocoa)
	include_directories(${COCOA})
endif()

if(UNIX AND NOT APPLE)
	find_package(X11 REQUIRED)
	link_libraries(${X11_LIBRARIES})
	include_directories(${X11_INCLUDE_DIR})
endif()

set(obs_libffutil_SOURCES
	../../../deps/libff/libff/ff-util.c
	)
set(obs_libffutil_HEADERS
	../../../deps/libff/libff/ff-util.h
	)

if(MSVC)
	set_source_files_properties(
		../../../deps/libff/libff/ff-util.c
		PROPERTIES COMPILE_FLAGS -Dinline=__inline
		)
endif()

set(ffmpeg-ouput-ui_HEADERS
	${ffmpeg-ouput-ui_HEADERS}
	${obs_libffutil_HEADERS}
	./FFmpegOutputUI.h
	ffmpeg-ui-main.h
	)
set(ffmpeg-ouput-ui_SOURCES
	${ffmpeg-ouput-ui_SOURCES}
	${obs_libffutil_SOURCES}
	./FFmpegOutputUI.cpp
	ffmpeg-ui-main.cpp
	)
set(ffmpeg-ouput-ui_UI
	${ffmpeg-ouput-ui_UI}
	forms/output.ui
	)

if(APPLE)
	set(ffmpeg-ouput-ui_PLATFORM_LIBS
		${COCOA})
endif()

qt5_wrap_ui(ffmpeg-ouput-ui_UI_HEADERS
	${ffmpeg-ouput-ui_UI})

add_library(ffmpeg-ouput-ui MODULE
	${ffmpeg-ouput-ui_HEADERS}
	${ffmpeg-ouput-ui_SOURCES}
	${ffmpeg-ouput-ui_UI_HEADERS}
	)
target_link_libraries(ffmpeg-ouput-ui
	${frontend-tools_PLATFORM_LIBS}
	obs-frontend-api
	Qt5::Widgets
	${FFMPEG_LIBRARIES}
	libobs)

install_obs_plugin_with_data(ffmpeg-ouput-ui data)

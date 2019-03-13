project(win-asio)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(LibObs REQUIRED)
find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)

include_directories(
	${CMAKE_SOURCE_DIR}/UI/obs-frontend-api
	${PORTAUDIO_INCLUDE_DIR}
	${Qt5Core_INCLUDES}
	${Qt5Widgets_INCLUDES}
)
set(win-asio-sdk_HEADERS
	)

set(win-asio_HEADERS
	circle-buffer.h
	asioselector.h
	)

set(win-asio_SOURCES
	asioselector.cpp
	asio-input.cpp
	)

set(win-asio_QRC
	asio-input.qrc
	)

qt5_add_resources(win-asio_QRC_SOURCES ${win-asio_QRC})

add_library(win-asio MODULE
	${win-asio_SOURCES}
	${win-asio_HEADERS}
	${win-asio-sdk_HEADERS}
	${win-asio_QRC_SOURCES}
)

target_link_libraries(win-asio
	libobs
	obs-frontend-api
	${PORTAUDIO_LIBRARY}
	Qt5::Core
	Qt5::Widgets
)

install_obs_plugin_with_data(win-asio data)

math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
add_custom_command(TARGET win-asio POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
	"${PORTAUDIO_INCLUDE_DIR}/../build/$<CONFIGURATION>/portaudio_x64.dll"
	"${CMAKE_BINARY_DIR}/rundir/$<CONFIGURATION>/obs-plugins/${BITS}bit/"
	COMMAND ${CMAKE_COMMAND} -E copy
	"${PORTAUDIO_INCLUDE_DIR}/../build/$<CONFIGURATION>/portaudio_x64.pdb"
	"${CMAKE_BINARY_DIR}/rundir/$<CONFIGURATION>/obs-plugins/${BITS}bit/"
)

project(win-asio)

set(CMAKE_PREFIX_PATH "${QTDIR}")

find_package(LibObs REQUIRED)

if(DISABLE_ASIO)
	message(STATUS "Asio support disabled")
	return()
endif()

find_path(BASS_ASIO_INCLUDE_DIR
	NAMES bassasio.h
	HINTS
		${DepsPath}
		${_BASS_ASIO_INCLUDE_DIRS}
	PATHS
)

find_library(BASS_ASIO_LIB
	NAMES ${_BASS_ASIO_LIBRARIES} bassasio
	HINTS
		${DepsPath}
		${_BASS_ASIO_LIBRARY_DIRS}
	PATHS
		/usr/lib /usr/local/lib /opt/local/lib /sw/lib
	PATH_SUFFIXES
		lib${_lib_suffix} lib
		libs${_lib_suffix} libs
		bin${_lib_suffix} bin
		../lib${_lib_suffix} ../lib
		../libs${_lib_suffix} ../libs
		../bin${_lib_suffix} ../bin)

if(BASS_ASIO_INCLUDE_DIR AND BASS_ASIO_LIB)
	message(STATUS "Found bass asio sdk: ${BASS_ASIO_INCLUDE_DIR}")
	message(STATUS "Found bass asio library: ${BASS_ASIO_LIB}")
else()
	message(STATUS "BAss Asio sdk not found")
endif(BASS_ASIO_INCLUDE_DIR AND BASS_ASIO_LIB)

include_directories(
	/
	SYSTEM "${CMAKE_SOURCE_DIR}/libobs"
	${BASS_ASIO_INCLUDE_DIR}
)
set(win-asio-sdk_HEADERS
	)

set(win-asio_HEADERS
	)

set(win-asio_SOURCES
	asio-input.cpp
)

add_library(win-asio MODULE
	${win-asio_SOURCES}
	${win-asio_HEADERS}
	${win-asio-sdk_HEADERS}
)
target_link_libraries(win-asio
	libobs
	${BASS_ASIO_LIB}
)

install_obs_plugin_with_data(win-asio data)

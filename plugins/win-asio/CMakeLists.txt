project(win-asio)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

include_directories(
	${CMAKE_SOURCE_DIR}/UI/obs-frontend-api
	${JUCE_INCLUDE_DIR}
	${JUCE_INCLUDE_DIR}/modules
)
set(juce-sdk_HEADERS
	)
set(juce-sdk_SOURCES
	)

set(win-asio_HEADERS
	)

set(win-asio_SOURCES
	asio-input.cpp
	)
set(JUCE_LIB optimized ${JUCE_LIBRARY} debug ${JUCE_LIBRARY_DEBUG})
set(win-asio_QRC
	asio-input.qrc
	)

qt5_add_resources(win-asio_QRC_SOURCES ${win-asio_QRC})

add_library(win-asio MODULE
	${win-asio_SOURCES}
	${win-asio_QRC_SOURCES}
)

target_link_libraries(win-asio
	libobs
	obs-frontend-api
	Qt5::Core
	Qt5::Widgets
	${JUCE_LIB}
)

install_obs_plugin_with_data(win-asio data)

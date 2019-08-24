project(obs-vst3)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(
	${CMAKE_SOURCE_DIR}/UI/obs-frontend-api
	${JUCE_INCLUDE_DIR}
	${JUCE_INCLUDE_DIR}/modules
)

set(obs-vst3_HEADERS
	obs-vst3.hpp
	)
set(obs-vst3_SOURCES
	obs-vst3.cpp
	)

add_library(obs-vst3 MODULE
	${obs-vst3_SOURCES}
	${obs-vst3_HEADERS}
)

set(JUCE_LIB optimized ${JUCE_LIBRARY} debug ${JUCE_LIBRARY_DEBUG})
set(JUCE_INCLUDE_DIRS ${JUCE_INCLUDE_DIR})

if (JUCE_LIB AND JUCE_INCLUDE_DIRS)
  set(JUCE_FOUND TRUE)
endif (JUCE_LIB AND JUCE_INCLUDE_DIRS)

if (JUCE_FOUND)
      message(STATUS "Found juce: ${JUCE_LIB}")
else (JUCE_FOUND)
 message(STATUS "Juce not found")
endif (JUCE_FOUND)

target_link_libraries(obs-vst3
	libobs
	obs-frontend-api
	Qt5::Core
	Qt5::Widgets
	${JUCE_LIB}
)

install_obs_plugin_with_data(obs-vst3 data)

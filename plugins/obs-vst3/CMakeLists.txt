project(obs-vst3)

add_library(obs-vst3 MODULE)
add_library(OBS::vst3 ALIAS obs-vst3)

target_sources(obs-vst3 PRIVATE obs-vst3.cpp)

if(NOT QT_VERSION)
  set(QT_VERSION
      "5"
      CACHE STRING "OBS Qt version [5, 6]" FORCE)
  set_property(CACHE QT_VERSION PROPERTY STRINGS 5 6)
endif()

find_qt(
  VERSION ${QT_VERSION}
  COMPONENTS Widgets Core)
  
set(JUCE_LIB optimized ${JUCE_LIBRARY} debug ${JUCE_LIBRARY_DEBUG})

target_include_directories(obs-vst3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_SOURCE_DIR}/UI/obs-frontend-api
	${JUCE_INCLUDE_DIR}
	${JUCE_INCLUDE_DIR}/modules
)
target_link_libraries(obs-vst3 PRIVATE
	OBS::libobs
	OBS::frontend-api
	Qt5::Core
	Qt5::Widgets
	${JUCE_LIB}
)

setup_plugin_target(obs-vst3)

project(win-asio)

add_library(win-asio MODULE)
add_library(OBS::asio ALIAS win-asio)
target_sources(win-asio PRIVATE asio-input.cpp)
target_sources(win-asio PRIVATE asio-input.qrc)

if(NOT QT_VERSION)
  set(QT_VERSION
      "5"
      CACHE STRING "OBS Qt version [5, 6]" FORCE)
  set_property(CACHE QT_VERSION PROPERTY STRINGS 5 6)
endif()

find_qt(
  VERSION ${QT_VERSION}
  COMPONENTS Widgets Core)

set_target_properties(
  win-asio
  PROPERTIES AUTOMOC ON
             AUTOUIC ON)

set(JUCE_LIB optimized ${JUCE_LIBRARY} debug ${JUCE_LIBRARY_DEBUG})

target_include_directories(win-asio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                       ${CMAKE_CURRENT_BINARY_DIR}
                                       ${CMAKE_SOURCE_DIR}/UI/obs-frontend-api
                                       ${JUCE_INCLUDE_DIR}
                                       ${JUCE_INCLUDE_DIR}/modules)

target_link_libraries(win-asio PRIVATE
	OBS::libobs
	OBS::frontend-api
	Qt5::Core
	Qt5::Widgets
	${JUCE_LIB}
)

setup_plugin_target(win-asio)

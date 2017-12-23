# Once done these will be defined:
#
#  RTAUDIO_ASIO_FOUND
#  RTAUDIO_INCLUDE_DIRS
#
# For use in OBS: 
#
#  RTAUDIO_INCLUDE_DIR

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(_rtaudio QUIET rtaudio)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_lib_suffix 64)
else()
	set(_lib_suffix 32)
endif()

find_path(RTAUDIO_INCLUDE_DIR
	NAMES RtAUdio.h
	HINTS
		ENV asioPath${_lib_suffix}
		ENV asioPath
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${asioPath${_lib_suffix}}
		${asioPath}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_ASIO_INCLUDE_DIRS}
	PATHS
		/usr/include /usr/local/include /opt/local/include /sw/include)

find_library(RTAUDIO_LIBRARY
	NAMES ${_RTAUDIO_LIBRARIES} rtaudio_static
	HINTS
		ENV rtaudioPath${_lib_suffix}
		ENV rtaudioPath
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${rtaudioPath${_lib_suffix}}
		${rtaudioPath}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_RTAUDIO_LIBRARY_DIRS}
	PATHS
		${_RTAUDIO_LIBDIR} /usr/lib /usr/local/lib /opt/local/lib /sw/lib
	PATH_SUFFIXES
		lib${_lib_suffix} lib
		libs${_lib_suffix} libs
		bin${_lib_suffix} bin
		../lib${_lib_suffix} ../lib
		../libs${_lib_suffix} ../libs
		../bin${_lib_suffix} ../bin
)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(rtaudio DEFAULT_MSG RTAUDIO_LIBRARY RTAUDIO_INCLUDE_DIR)
mark_as_advanced(RTAUDIO_INCLUDE_DIR RTAUDIO_LIBRARY)

if (RTAUDIO_LIBRARY AND RTAUDIO_INCLUDE_DIR)
	set(RTAUDIO_ASIO_FOUND TRUE)

	set(RTAUDIO_INCLUDE_DIRS
		${RTAUDIO_INCLUDE_DIR}
	)

	set(RTAUDIO_LIBRARIES
		${RTAUDIO_LIBRARIES}
		${RTAUDIO_LIBRARY}
	)
	message(STATUS "Found rtaudio with asio support: ${RTAUDIO_INCLUDE_DIR}")
	message(STATUS "Found rtaudio lib with asio support: ${RTAUDIO_LIBRARY}")
else()
	message(STATUS "RTaudio not found: asio support disabled")
endif (RTAUDIO_LIBRARY AND RTAUDIO_INCLUDE_DIR)


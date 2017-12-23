# Once done these will be defined:
#
#  ASIO_FOUND
#  ASIOSDK_INCLUDE_DIRS
#
# For use in OBS: 
#
#  ASIO_INCLUDE_DIR

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(_asio QUIET asio)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_lib_suffix 64)
else()
	set(_lib_suffix 32)
endif()

find_path(ASIO_INCLUDE_DIR
	NAMES asiosdk2.3/common/asio.h
	HINTS
		ENV asiosdkPath${_lib_suffix}
		ENV asiosdkPath
		ENV DepsPath${_lib_suffix}
		ENV DepsPath
		${asiosdkPath${_lib_suffix}}
		${asiosdkPath}
		${DepsPath${_lib_suffix}}
		${DepsPath}
		${_ASIOSDK_INCLUDE_DIRS}
	PATHS
		/usr/include /usr/local/include /opt/local/include /sw/include)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(asio DEFAULT_MSG ASIO_INCLUDE_DIR)
mark_as_advanced(ASIO_INCLUDE_DIR)

if(ASIO_FOUND)
	set(ASIOSDK_INCLUDE_DIRS ${ASIO_INCLUDE_DIR})
	message(STATUS "Found asio sdk: ${ASIO_INCLUDE_DIR}")
else()
	message(STATUS "Asio sdk not found")
endif(ASIO_FOUND)



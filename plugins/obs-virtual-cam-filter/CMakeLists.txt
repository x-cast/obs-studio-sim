project(virtual-cam-filter VERSION 0.0.2)
set(PROJECT_FULL_NAME "Virtual Camera Filter")

if(MSVC)
	set(virtual-cam-filter_PLATFORM_DEPS
		w32-pthreads)
endif()

set(virtual-cam-filter_HEADERS
	virtual-cam-filter.h)
set(virtual-cam-filter_SOURCES
	virtual-cam-filter.c)

if(WIN32)
	configure_file(resource.rc.in virtual-cam-filter.rc)
	list(APPEND virtual-cam-filter_SOURCES
		virtual-cam-filter.rc)
endif()

add_library(virtual-cam-filter MODULE
	${virtual-cam-filter_HEADERS}
	${virtual-cam-filter_SOURCES})
target_link_libraries(virtual-cam-filter
	libobs
	${virtual-cam-filter_PLATFORM_DEPS})

install_obs_plugin_with_data(virtual-cam-filter data)

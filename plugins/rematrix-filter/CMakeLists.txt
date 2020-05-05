cmake_minimum_required(VERSION 3.5)

project(rematrix-filter)

set(rematrix-filter_SOURCES
	rematrix-filter.c
)

add_library(rematrix-filter MODULE
	${rematrix-filter_SOURCES}
)

target_link_libraries(rematrix-filter
	libobs
)

install_obs_plugin_with_data(rematrix-filter data)
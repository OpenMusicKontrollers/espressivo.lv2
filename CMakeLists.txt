cmake_minimum_required(VERSION 2.8)

project(espressivo.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/props.lv2)
include_directories(${PROJECT_SOURCE_DIR}/xpress.lv2)

set(CMAKE_C_FLAGS "-fdata-sections -ffunction-sections ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wredundant-decls -Wmissing-prototypes -Wstrict-prototypes -Wno-attributes ${CMAKE_C_FLAGS}")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,defs ${CMAKE_MODULE_LINKER_FLAGS}")
	set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,nodelete ${CMAKE_MODULE_LINKER_FLAGS}")
elseif(WIN32)
	set(CMAKE_C_FLAGS "-mstackrealign ${CMAKE_C_FLAGS}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
	if(APPLE)
		set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-dead_strip ${CMAKE_MODULE_LINKER_FLAGS}")
	else()
		set(CMAKE_MODULE_LINKER_FLAGS "-Wl,--gc-sections -Wl,-s ${CMAKE_MODULE_LINKER_FLAGS}")
	endif()
endif()

file(STRINGS "VERSION" ESPRESSIVO_VERSION)
string(REPLACE "." ";" VERSION_LIST ${ESPRESSIVO_VERSION})
list(GET VERSION_LIST 0 ESPRESSIVO_MAJOR_VERSION)
list(GET VERSION_LIST 1 ESPRESSIVO_MINOR_VERSION)
list(GET VERSION_LIST 2 ESPRESSIVO_MICRO_VERSION)
add_definitions("-DESPRESSIVO_VERSION=\"${ESPRESSIVO_VERSION}\"")

if(NOT DEFINED PLUGIN_DEST)
	set(PLUGIN_DEST lib/lv2/espressivo.lv2)
endif()

find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

set(LIBS m) # math library

pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})
set(LIBS ${LIBS} ${LV2_LDFLAGS})

add_library(espressivo MODULE
	espressivo.c
	midi_in.c
	mpe_in.c
	tuio2_in.c
	tuio2_out.c
	sampleandhold_flt.c
	through_flt.c
	modulator_flt.c
	chord_flt.c
	reducto_flt.c
	discreto_flt.c
	sqew_flt.c
	mpe_out.c
	sc_out.c
	monitor_out.c)
target_link_libraries(espressivo ${LIBS})
set_target_properties(espressivo PROPERTIES PREFIX "")
install(TARGETS espressivo DESTINATION ${PLUGIN_DEST})

configure_file(${PROJECT_SOURCE_DIR}/manifest.ttl.in
	${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${PLUGIN_DEST})
install(FILES ${PROJECT_SOURCE_DIR}/espressivo.ttl DESTINATION ${PLUGIN_DEST})

cmake_minimum_required(VERSION 2.8)

project(espressivo.lv2)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/osc.lv2)
include_directories(${PROJECT_SOURCE_DIR}/props.lv2)
include_directories(${PROJECT_SOURCE_DIR}/xpress.lv2)

set(CMAKE_C_FLAGS "-std=gnu11 -Wextra -Wno-unused-parameter -ffast-math -fvisibility=hidden ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wshadow -Wimplicit-function-declaration -Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "-Wl,-z,nodelete ${CMAKE_MODULE_LINKER_FLAGS}")
add_definitions("-D_GNU_SOURCE=1") # asprintf

set(ESPRESSIVO_MAJOR_VERSION 0)
set(ESPRESSIVO_MINOR_VERSION 1)
set(ESPRESSIVO_MICRO_VERSION 1)
set(ESPRESSIVO_VERSION "${}.${}.${}")

set(DEST lib/lv2/espressivo.lv2)

find_package(PkgConfig) # ${PKG_CONFIG_FOUND}

pkg_search_module(LV2 REQUIRED lv2>=1.10)
include_directories(${LV2_INCLUDE_DIRS})
set(LIBS ${LIBS} ${LV2_LDFLAGS})

include(CheckCSourceCompiles)
CHECK_C_SOURCE_COMPILES("int main(int argc, char **argv)
	{void *src; void *dst = __builtin_assume_aligned(src, 16); return 0;}"
	HAS_BUILTIN_ASSUME_ALIGNED)
if(${HAS_BUILTIN_ASSUME_ALIGNED})
	add_definitions("-DHAS_BUILTIN_ASSUME_ALIGNED")
endif()

add_library(espressivo MODULE
	espressivo.c
	midi_in.c
	tuio2_in.c
	sample_and_hold.c
	through.c
	mpe_out.c
	sc_out.c)
target_link_libraries(espressivo ${LIBS})
set_target_properties(espressivo PROPERTIES PREFIX "")
install(TARGETS espressivo DESTINATION ${DEST})

configure_file(${PROJECT_SOURCE_DIR}/manifest.ttl.in
	${PROJECT_BINARY_DIR}/manifest.ttl)
install(FILES ${PROJECT_BINARY_DIR}/manifest.ttl DESTINATION ${DEST})
install(FILES ${PROJECT_SOURCE_DIR}/espressivo.ttl DESTINATION ${DEST})

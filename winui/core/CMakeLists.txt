# evo-core: mail engine of the native Windows client (see evo-core.h).
#
# Built with win32/msvc/build-winui-core.bat, against the MSVC builds of
# evolution-data-server and its dependencies (common prefix).

cmake_minimum_required(VERSION 3.20)

project(evo-core
	VERSION 0.1.0
	LANGUAGES C
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CORE_DEPS REQUIRED
	camel-1.2
	libedataserver-1.2
	libecal-2.0
	libebook-1.2
	json-glib-1.0
	gio-2.0
)

# Thunderbird's ISPDB (thunderbird/autoconfig, MPL 2.0, see ispdb/README.md)
# embedded as resources: known providers for the account detection
file(GLOB ISPDB_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/ispdb" "${CMAKE_CURRENT_SOURCE_DIR}/ispdb/*.xml")
set(ISPDB_GRESOURCE "${CMAKE_CURRENT_BINARY_DIR}/ispdb.gresource.xml")
set(_ispdb_xml "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gresources>\n  <gresource prefix=\"/org/evolution-win/ispdb\">\n")
foreach(_file ${ISPDB_FILES})
	string(APPEND _ispdb_xml "    <file>${_file}</file>\n")
endforeach()
string(APPEND _ispdb_xml "  </gresource>\n</gresources>\n")
file(WRITE "${ISPDB_GRESOURCE}" "${_ispdb_xml}")

pkg_get_variable(GLIB_COMPILE_RESOURCES gio-2.0 glib_compile_resources)
if(NOT GLIB_COMPILE_RESOURCES OR NOT EXISTS "${GLIB_COMPILE_RESOURCES}")
	find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
endif()
list(TRANSFORM ISPDB_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/ispdb/" OUTPUT_VARIABLE ISPDB_PATHS)
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ispdb-resources.c"
	COMMAND "${GLIB_COMPILE_RESOURCES}" --generate-source --c-name evo_core_ispdb
		--sourcedir "${CMAKE_CURRENT_SOURCE_DIR}/ispdb"
		--target "${CMAKE_CURRENT_BINARY_DIR}/ispdb-resources.c" "${ISPDB_GRESOURCE}"
	DEPENDS "${ISPDB_GRESOURCE}" ${ISPDB_PATHS}
)

add_library(evo-core SHARED
	evo-core.h
	evo-core.c
	"${CMAKE_CURRENT_BINARY_DIR}/ispdb-resources.c"
)

# evolution-ews is loaded at run time (Camel provider, registry modules),
# not linked: its version comes from its sources, for the "About" dialog
set(EWS_VERSION "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../evolution-ews/CMakeLists.txt")
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../evolution-ews/CMakeLists.txt" _ews_cmake)
	string(REGEX MATCH "project\\(evolution-ews[ \t\r\n]+VERSION ([0-9.]+)" _ews_match "${_ews_cmake}")
	set(EWS_VERSION "${CMAKE_MATCH_1}")
endif()

target_compile_definitions(evo-core PRIVATE
	-DG_LOG_DOMAIN=\"evo-core\"
	-DEVO_CORE_EWS_VERSION=\"${EWS_VERSION}\"
)

target_include_directories(evo-core PRIVATE ${CORE_DEPS_INCLUDE_DIRS})
target_compile_options(evo-core PRIVATE ${CORE_DEPS_CFLAGS_OTHER})
target_link_libraries(evo-core PRIVATE ${CORE_DEPS_LDFLAGS} dnsapi)

# console check of the engine against the configured accounts
add_executable(evo-core-test evo-core-test.c)
target_link_libraries(evo-core-test PRIVATE evo-core)

install(TARGETS evo-core evo-core-test
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
install(FILES evo-core.h DESTINATION include/evo-core)

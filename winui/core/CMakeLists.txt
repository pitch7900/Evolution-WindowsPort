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
)

add_library(evo-core SHARED
	evo-core.h
	evo-core.c
)

target_compile_definitions(evo-core PRIVATE
	-DG_LOG_DOMAIN=\"evo-core\"
)

target_include_directories(evo-core PRIVATE ${CORE_DEPS_INCLUDE_DIRS})
target_compile_options(evo-core PRIVATE ${CORE_DEPS_CFLAGS_OTHER})
target_link_libraries(evo-core PRIVATE ${CORE_DEPS_LDFLAGS})

# console check of the engine against the configured accounts
add_executable(evo-core-test evo-core-test.c)
target_link_libraries(evo-core-test PRIVATE evo-core)

install(TARGETS evo-core evo-core-test
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
install(FILES evo-core.h DESTINATION include/evo-core)
